#include "callback_diagnostic.hpp"
#if YW1_EGL_CONTEXT_PROBE
#include "context_diagnostic.hpp"
#endif
#include "diagnostics/callback_wire.hpp"
#include "diagnostics/egl_snapshot.hpp"
#include "hooking/callsite_installer.hpp"
#include "logging/memory_log.hpp"
#include <common.hpp>
#include <lib/util/sys/cur_proc_handle.hpp>
#include <lib/util/sys/mem_layout.hpp>

extern "C" char __module_start;
extern "C" yw1::diagnostics::EglBoolean yw1_callback_entry(
    yw1::diagnostics::EglHandle display, yw1::diagnostics::EglHandle surface);
extern "C" char yw1_callback_original_call;

namespace yw1::bootstrap {
namespace {
using namespace diagnostics;
using namespace hooking;
using Field = CallbackField;
constexpr std::uint32_t kPltWords[]{0xD00012B0, 0xF941CE11, 0x910E6210, 0xD61F0220};
constinit CallsiteInstaller installer;

template <std::size_t Size> void logResult(const char (&label)[Size], Result result) {
    static_cast<void>(logging::memoryLog().appendResult(label, Size - 1, result));
}
Result queryCode(std::uintptr_t address, std::uintptr_t lower, std::uint64_t size) {
    if (!contains(lower, size, address & ~std::uintptr_t{4095}, 4096)) {
        return error(HookError::Boundary);
    }
    MemoryInfo info{};
    u32 page = 0;
    const auto result = svcQueryMemory(&info, &page, address);
    if (result != 0) { return result; }
    if (info.type != MemType_CodeStatic || info.perm != Perm_Rx || info.attr != 0 ||
        info.ipc_refcount != 0 || info.device_refcount != 0 ||
        !contains(info.addr, info.size, address & ~std::uintptr_t{4095}, 4096)) {
        return error(HookError::InvalidRegion);
    }
    return 0;
}

// 予約なしの空きpage探索を有限回行い、競合はMapの実Resultで拒否する。
// upstream RwPagesのabort・乱択の無限内loop・destructor依存を使用しない。
class PageAlias {
  public:
    Result map(Handle process, std::uintptr_t source) {
        source_ = source & ~std::uintptr_t{4095};
        process_ = process;
        const auto& aslr = exl::util::mem_layout::s_Aslr;
        const auto& alias = exl::util::mem_layout::s_Alias;
        const auto& heap = exl::util::mem_layout::s_Heap;
        if (aslr.m_Start > UINTPTR_MAX - aslr.m_Size) {
            return error(HookError::InvalidRegion);
        }
        auto cursor = aslr.m_Start;
        for (unsigned attempt = 0; attempt < 4096 && cursor < aslr.GetEnd(); ++attempt) {
            MemoryInfo info{};
            u32 page = 0;
            auto result = svcQueryMemory(&info, &page, cursor);
            if (result != 0) { return result; }
            if (info.addr > cursor || info.size == 0 || info.addr > UINTPTR_MAX - info.size ||
                info.addr + info.size <= cursor) {
                return error(HookError::InvalidRegion);
            }
            const auto end = info.addr + info.size;
            if (info.type == MemType_Unmapped && (cursor | end) % 4096 == 0) {
                for (unsigned excluded = 0; excluded < 3; ++excluded) {
                    if (cursor >= end || end - cursor < 4096) { break; }
                    if (cursor < alias.GetEnd() && alias.m_Start < cursor + 4096) {
                        cursor = alias.GetEnd();
                    } else if (cursor < heap.GetEnd() && heap.m_Start < cursor + 4096) {
                        cursor = heap.GetEnd();
                    } else {
                        if (!contains(aslr.m_Start, aslr.m_Size, cursor, 4096)) {
                            return error(HookError::InvalidRegion);
                        }
                        destination_ = reinterpret_cast<void*>(cursor);
                        result = svcMapProcessMemory(destination_, process_, source_, 4096);
                        logResult("[YW1] callback_alias_map_result=", result);
                        if (result != 0) { destination_ = nullptr; }
                        return result;
                    }
                }
            }
            cursor = end;
        }
        return error(HookError::Capacity);
    }
    std::uint32_t* word(std::uintptr_t source) {
        return reinterpret_cast<std::uint32_t*>(
            static_cast<unsigned char*>(destination_) + source - source_);
    }
    Result unmap() {
        const auto result = svcUnmapProcessMemory(destination_, process_, source_, 4096);
        logResult("[YW1] callback_alias_unmap_result=", result);
        if (result == 0) { destination_ = nullptr; }
        // 失敗時は暗黙に再利用・再Unmapせず、process終了まで資源を保持する。
        return result;
    }
  private:
    Handle process_ = INVALID_HANDLE;
    std::uintptr_t source_ = 0;
    void* destination_ = nullptr;
};

Result synchronizeCode(std::uintptr_t rw, std::uintptr_t rx, std::size_t size) {
    if (size == 0 || size > 4096 || rw > UINTPTR_MAX - size || rx > UINTPTR_MAX - size) {
        return error(HookError::InvalidRegion);
    }
    // 対象MesosphereのSCTLR.UCI/UCTと通常cacheable NSO mappingを前提とする。
    std::uint64_t ctr = 0;
    asm volatile("mrs %0, ctr_el0" : "=r"(ctr));
    const std::uintptr_t dataLine = 4u << ((ctr >> 16) & 15);
    const std::uintptr_t instructionLine = 4u << (ctr & 15);
    for (auto address = rw & ~(dataLine - 1); address < rw + size; address += dataLine) {
        asm volatile("dc cvau, %0" : : "r"(address) : "memory");
    }
    asm volatile("dsb ish" : : : "memory");
    for (auto address = rx & ~(instructionLine - 1); address < rx + size;
         address += instructionLine) {
        asm volatile("ic ivau, %0" : : "r"(address) : "memory");
    }
    asm volatile("dsb ish\n\tisb" : : : "memory");
    // API Resultではなく、検証済みCPU命令列を最後まで発行したMOD stageの完了値。
    return 0;
}

class NativeCallsitePlatform : public CallsitePlatform {
  public:
    NativeCallsitePlatform(Handle process, const exl::util::ModuleInfo& main,
                           const exl::util::ModuleInfo& self, CallbackWire& report)
        : process_(process), main_(main), self_(self), report_(report) {}
    Result validate(const CallsiteTarget& target) override {
        target_ = target.main.base + kSwapCallOffset;
        original_ = target.originalSlot;
        if (target.callback != reinterpret_cast<std::uintptr_t>(&yw1_callback_entry) ||
            original_ != reinterpret_cast<std::uintptr_t>(&yw1_callback_original_call) ||
            !contains(self_.m_Text.m_Start, self_.m_Text.m_Size, target.callback, 52) ||
            original_ != target.callback + 24 || target.callback % 4096 != 0) {
            return error(HookError::InvalidPlatform);
        }
        auto result = queryCode(target_, main_.m_Text.m_Start, main_.m_Text.m_Size);
        if (result == 0) {
            result = queryCode(original_, self_.m_Text.m_Start, self_.m_Text.m_Size);
        }
        if (result == 0) {
            result = queryCode(target.main.base + kSwapPltOffset,
                               main_.m_Text.m_Start, main_.m_Text.m_Size);
        }
        if (result != 0) { return result; }
        const auto* plt = reinterpret_cast<const std::uint32_t*>(target.main.base + kSwapPltOffset);
        for (unsigned i = 0; i < 4; ++i) {
            if (__atomic_load_n(plt + i, __ATOMIC_ACQUIRE) != kPltWords[i]) {
                return error(HookError::Instructions);
            }
        }
        return __atomic_load_n(reinterpret_cast<const std::uint32_t*>(original_),
                                __ATOMIC_ACQUIRE) == 0x94000000u
                   ? 0 : error(HookError::Instructions);
    }
    Result readTarget(std::uint32_t& word) override {
        const auto result = queryCode(target_, main_.m_Text.m_Start, main_.m_Text.m_Size);
        if (result == 0) {
            word = __atomic_load_n(reinterpret_cast<const std::uint32_t*>(target_), __ATOMIC_ACQUIRE);
        }
        return result;
    }
    Result prepareOriginal(const CallsitePlan& plan) override {
        PageAlias alias;
        auto result = alias.map(process_, original_);
        if (result != 0) { return result; }
        auto* destination = alias.word(original_);
        if (__atomic_load_n(destination, __ATOMIC_ACQUIRE) != 0x94000000u) {
            result = error(HookError::Instructions);
        } else {
            __atomic_store_n(destination, plan.originalBranch, __ATOMIC_RELEASE);
            report_[Field::ResourcesRetained] = 1;
            if (__atomic_load_n(destination, __ATOMIC_ACQUIRE) != plan.originalBranch) {
                result = error(HookError::Readback);
            } else {
                result = synchronizeCode(reinterpret_cast<std::uintptr_t>(destination), original_, 4);
            }
        }
        const auto cleanup = alias.unmap();
        if (cleanup != 0) { report_[Field::CleanupResult] = cleanup; }
        return result != 0 ? result : cleanup;
    }
    Result mapTarget() override { return targetAlias_.map(process_, target_); }
    Result exchange(std::uint32_t expected, std::uint32_t replacement, bool& changed) override {
        changed = false;
        auto* destination = targetAlias_.word(target_);
        for (unsigned attempt = 0; attempt < 4; ++attempt) {
            auto observed = expected;
            if (__atomic_compare_exchange_n(destination, &observed, replacement, true,
                                             __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                changed = true;
                return 0;
            }
            if (observed != expected) { return error(HookError::Instructions); }
        }
        return error(HookError::Busy);
    }
    Result synchronizeTarget() override {
        return synchronizeCode(reinterpret_cast<std::uintptr_t>(targetAlias_.word(target_)), target_, 4);
    }
    Result unmapTarget() override { return targetAlias_.unmap(); }
    void publish(const CallbackWire& wire) override { publishCallbackConfiguration(wire); }
  private:
    Handle process_;
    const exl::util::ModuleInfo& main_;
    const exl::util::ModuleInfo& self_;
    CallbackWire& report_;
    PageAlias targetAlias_;
    std::uintptr_t target_ = 0, original_ = 0;
};
} // 無名名前空間

void runCallbackDiagnostic(std::uint32_t eglGuard) {
    constexpr auto profile = static_cast<CallbackProfile>(YW1_CALLBACK_PROFILE);
    constexpr char kBanner[] =
#if YW1_CALLBACK_PROFILE == 2
        "\n[YW1] B0.2.8 profile=B callback_canary_requested\n";
#else
        "\n[YW1] B0.2.8 profile=A read_only_no_patch\n";
#endif
    static_cast<void>(logging::memoryLog().append(kBanner, sizeof(kBanner) - 1));
    auto wire = initialCallbackWire(profile);
    wire[Field::GuardResult] = error(HookError::Identity);
    const auto finish = [&] {
        publishCallbackConfiguration(wire);
        logResult("[YW1] callback_guard_result=", wire[Field::GuardResult]);
        logResult("[YW1] callback_install_result=", wire[Field::InstallResult]);
    };
    using exl::util::ModuleIndex;
    const exl::util::ModuleInfo* self = nullptr;
    for (unsigned i = 0; i < static_cast<unsigned>(ModuleIndex::End); ++i) {
        const auto index = static_cast<ModuleIndex>(i);
        if (exl::util::HasModule(index)) {
            const auto& module = exl::util::GetModuleInfo(index);
            if (module.m_Total.m_Start == reinterpret_cast<std::uintptr_t>(&__module_start)) {
                self = &module;
            }
        }
    }
    if (eglGuard != 0 || self == nullptr || !exl::util::HasModule(ModuleIndex::Main) ||
        !exl::util::HasModule(ModuleIndex::Subsdk0)) {
        wire[Field::HookError] = wire[Field::GuardResult];
        wire[Field::InitializationStage] = 6;
        finish();
        return;
    }
    const auto& main = exl::util::GetModuleInfo(ModuleIndex::Main);
    const auto& sub = exl::util::GetModuleInfo(ModuleIndex::Subsdk0);
    const auto handle = exl::util::proc_handle::Get();
    if (handle == INVALID_HANDLE) {
        wire[Field::HookError] = error(HookError::InvalidPlatform);
        finish();
        return;
    }
    CallsiteTarget target{
        {main.m_Total.m_Start, main.m_Total.m_Size,
         {kTitleId, "main", std::to_array(kMainBuildId)}},
        {sub.m_Total.m_Start, sub.m_Total.m_Size, {kTitleId, "subsdk0", kSubsdk0Id}},
        main.m_Text.m_Size, reinterpret_cast<std::uintptr_t>(&yw1_callback_entry),
        reinterpret_cast<std::uintptr_t>(&yw1_callback_original_call),
        CallbackThreadPolicy::AtomicDirectBl};
    NativeCallsitePlatform platform(handle, main, *self, wire);
    if constexpr (profile == CallbackProfile::Canary) {
#if YW1_EGL_CONTEXT_PROBE
        prepareContextDiagnostic();
#endif
        static_cast<void>(installer.install(target, platform, wire));
    } else {
        CallsitePlan plan;
        auto result = prepareCallsite(target, plan);
        if (result == 0) { result = platform.validate(target); }
        std::uint32_t word = 0;
        if (result == 0) {
            result = platform.readTarget(word);
            if (result == 0 && word != plan.expected) { result = error(HookError::Instructions); }
        }
        wire[Field::GuardResult] = result;
        wire[Field::Target] = plan.target;
        wire[Field::Original] = plan.original;
        wire[Field::ThreadPolicy] = 1;
        wire[Field::HookError] = result;
        wire[Field::InitializationStage] = result == 0 ? 5 : 6;
    }
    finish();
}
} // 名前空間 yw1::bootstrap

#if YW1_NATIVE_RENDER_EXPERIMENT
extern "C" void yw1_canary_enter(std::uintptr_t swapDisplay, std::uintptr_t swapSurface) {
#else
extern "C" void yw1_canary_enter() {
#endif
    yw1::diagnostics::recordCallbackEnter();
#if YW1_EGL_CONTEXT_PROBE
#if YW1_NATIVE_RENDER_EXPERIMENT
    yw1::bootstrap::probeContextCallback(swapDisplay, swapSurface);
#else
    yw1::bootstrap::probeContextCallback();
#endif
#endif
}
extern "C" void yw1_canary_return(std::uint32_t result) {
    yw1::diagnostics::recordCallbackReturn(result);
}
