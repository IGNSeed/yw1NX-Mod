#include "exlaunch_preflight.hpp"

#include <common.hpp>
#include <initializer_list>
#include <lib/util/module_index.hpp>

namespace yw1::bootstrap {
namespace {

constexpr Result kInvalidLayout = 0xE201;
constexpr std::size_t kModuleCount = 6;
constexpr std::size_t kRegionLimit = 4096;
static_assert(static_cast<unsigned>(exl::util::ModuleIndex::Main) == 1);
static_assert(static_cast<unsigned>(exl::util::ModuleIndex::End) > kModuleCount);

struct Region {
    u64 start = 0;
    u64 size = 0;
    u64 end() const {
        return start + size;
    }
};

bool valid(const Region& region) {
    return region.size != 0 && region.start <= UINT64_MAX - region.size &&
           (region.start | region.size) % 4096 == 0;
}

bool overlaps(u64 start, u64 size, const Region& region) {
    return start < region.end() && region.start < start + size;
}

bool freeFits(u64 start, u64 end, u64 size, const Region& aslr, const Region& alias,
              const Region& heap) {
    start = start < aslr.start ? aslr.start : start;
    end = end > aslr.end() ? aslr.end() : end;
    for (unsigned attempt = 0; attempt < 3 && start < end && size <= end - start; ++attempt) {
        if (overlaps(start, size, alias)) {
            start = alias.end();
        } else if (overlaps(start, size, heap)) {
            start = heap.end();
        } else {
            return true;
        }
    }
    return false;
}

Result info(Region& region, InfoType address, InfoType size) {
    auto result = svcGetInfo(&region.start, address, CUR_PROCESS_HANDLE, 0);
    if (result == 0) {
        result = svcGetInfo(&region.size, size, CUR_PROCESS_HANDLE, 0);
    }
    return result != 0 ? result : (valid(region) ? 0 : kInvalidLayout);
}

} // 無名名前空間

std::uint32_t checkExlaunchEnvironment(const void* moduleStart) {
    Region alias, heap, aslr, stack;
    for (auto result : {info(alias, InfoType_AliasRegionAddress, InfoType_AliasRegionSize),
                        info(heap, InfoType_HeapRegionAddress, InfoType_HeapRegionSize),
                        info(aslr, InfoType_AslrRegionAddress, InfoType_AslrRegionSize),
                        info(stack, InfoType_StackRegionAddress, InfoType_StackRegionSize)}) {
        if (result != 0) {
            return result;
        }
    }
    // 上流と同じRX/R/RWの順序を確認し、Releaseで無効なassertへ依存しない。
    Region text, ro;
    Region main;
    unsigned state = 0;
    std::size_t modules = 0;
    std::size_t selfIndex = kModuleCount;
    u64 cursor = 0;
    bool wrapped = false;
    for (std::size_t count = 0; count < kRegionLimit; ++count) {
        MemoryInfo memory{};
        u32 page = 0;
        const auto result = svcQueryMemory(&memory, &page, cursor);
        if (result != 0) {
            return result;
        }
        if (memory.addr != cursor || memory.size == 0 || (memory.addr | memory.size) % 4096) {
            return kInvalidLayout;
        }
        const auto next = memory.addr + memory.size;
        if (next <= cursor) {
            // 対象kernelのQueryInfoは空間外をInaccessible(Reserved)の末尾blockで返す。
            wrapped = next == 0 && (memory.type & MemState_Type) == MemType_Reserved &&
                      memory.attr == 0 && memory.perm == 0 && cursor >= aslr.end() &&
                      cursor >= alias.end() && cursor >= heap.end() && cursor >= stack.end();
            break;
        }
        const auto type = memory.type & MemState_Type;
        if (state == 0 && type == MemType_CodeStatic && memory.perm == Perm_Rx) {
            text = {memory.addr, memory.size};
            state = 1;
        } else if (state == 1 && type == MemType_CodeStatic && memory.perm == Perm_R &&
                   memory.addr == text.end() && memory.attr == 0) {
            ro = {memory.addr, memory.size};
            state = 2;
        } else if (state == 2 && type == MemType_CodeMutable && memory.perm == Perm_Rw &&
                   memory.addr == ro.end()) {
            if (modules >= kModuleCount || text.size < 8 || ro.end() <= text.start) {
                return kInvalidLayout;
            }
            const auto* header = reinterpret_cast<const std::uint32_t*>(text.start);
            const auto modOffset = header[1];
            if (modOffset % 4 || modOffset > ro.end() - text.start - 28 ||
                *reinterpret_cast<const std::uint32_t*>(text.start + modOffset) != 0x30444F4D) {
                return kInvalidLayout;
            }
            if (modules == 1) {
                main = {text.start, ro.end() - text.start};
            }
            if (text.start == reinterpret_cast<std::uintptr_t>(moduleStart)) {
                selfIndex = modules;
            }
            ++modules;
            state = 0;
        } else if (state != 0) {
            return kInvalidLayout;
        }
        if (type == MemType_CodeStatic && memory.attr != 0) {
            return kInvalidLayout;
        }
        cursor = next;
    }
    if (!wrapped || state != 0 || modules != kModuleCount || selfIndex != 4 || !valid(main) ||
        main.size > aslr.size / 16) {
        return kInvalidLayout;
    }
    // 小さな空き穴だけでは上流の乱択が失敗しやすい。十分広い空きがある場合だけ進む。
    const auto minimumFree = (aslr.size / 16 + main.size + 4095) & ~u64{4095};
    cursor = aslr.start;
    for (std::size_t count = 0; count < kRegionLimit && cursor < aslr.end(); ++count) {
        MemoryInfo memory{};
        u32 page = 0;
        const auto result = svcQueryMemory(&memory, &page, cursor);
        if (result != 0) {
            return result;
        }
        const auto next = memory.addr + memory.size;
        if (memory.size == 0 || memory.addr > cursor || next <= cursor) {
            return kInvalidLayout;
        }
        if ((memory.type & MemState_Type) == MemType_Unmapped &&
            freeFits(cursor, next, minimumFree, aslr, alias, heap)) {
            return 0;
        }
        cursor = next;
    }
    return kInvalidLayout;
}

} // 名前空間 yw1::bootstrap
