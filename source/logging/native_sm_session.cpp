#include "sm_session.hpp"
#include <common.hpp>
#include <lib/util/sys/mem_layout.hpp>

namespace yw1::logging {
namespace {
class NativeMemory final : public SmSessionMemory {
  public:
    NativeMemory(std::uint64_t rxBase, std::uint64_t rxSize) : rxBase_(rxBase), rxSize_(rxSize) {}
    bool read(std::uint64_t address, std::span<std::byte> bytes) override {
        if (!address || address % 4 || bytes.empty() || bytes.size() % 4 ||
            bytes.size() > 128 || address > UINTPTR_MAX - bytes.size()) { return false; }
        MemoryInfo info{}; u32 page = 0;
        const auto permission = address >= rxBase_ && address - rxBase_ < rxSize_ ? Perm_Rx : Perm_R;
        if (svcQueryMemory(&info, &page, address) != 0 || info.attr != 0 ||
            (info.perm & permission) != permission || address < info.addr ||
            address - info.addr > info.size || bytes.size() > info.size - (address - info.addr)) {
            return false;
        }
        for (std::size_t i = 0; i < bytes.size();) {
            if ((address + i) % 8 == 0 && bytes.size() - i >= 8) {
                const auto value = __atomic_load_n(
                    reinterpret_cast<const std::uint64_t*>(address + i), __ATOMIC_ACQUIRE);
                for (unsigned j = 0; j < 8; ++j) { bytes[i + j] = std::byte(value >> (j * 8)); }
                i += 8;
            } else {
                const auto value = __atomic_load_n(
                    reinterpret_cast<const std::uint32_t*>(address + i), __ATOMIC_ACQUIRE);
                for (unsigned j = 0; j < 4; ++j) { bytes[i + j] = std::byte(value >> (j * 8)); }
                i += 4;
            }
        }
        return true;
    }
  private:
    std::uint64_t rxBase_, rxSize_;
};
} // 無名名前空間

Result loadNativeSmSession(Handle& session) {
    using exl::util::ModuleIndex;
    session = 0;
    if (!exl::util::HasModule(ModuleIndex::Sdk)) { return kSmIdentity; }
    const auto& sdk = exl::util::GetModuleInfo(ModuleIndex::Sdk);
    constexpr std::uint64_t noteOffset = 0xBDA02C;
    if (sdk.m_Total.m_Start > UINTPTR_MAX - noteOffset) { return kSmIdentity; }
    const auto address = sdk.m_Total.m_Start + noteOffset;
    if (address < sdk.m_Rodata.m_Start || address - sdk.m_Rodata.m_Start > sdk.m_Rodata.m_Size ||
        sdk.m_Rodata.m_Size - (address - sdk.m_Rodata.m_Start) < 36) { return kSmIdentity; }
    NativeMemory memory(sdk.m_Text.m_Start, sdk.m_Text.m_Size);
    std::array<std::byte, 36> note{};
    if (!memory.read(address, note)) { return kSmIdentity; }
    constexpr unsigned char header[]{4,0,0,0,20,0,0,0,3,0,0,0,'G','N','U',0};
    for (unsigned i = 0; i < sizeof(header); ++i) {
        if (note[i] != std::byte(header[i])) { return kSmIdentity; }
    }
    game::BuildId id{};
    for (unsigned i = 0; i < 20; ++i) { id[i] = std::to_integer<unsigned char>(note[16 + i]); }
    const game::ModuleImage image{sdk.m_Total.m_Start, sdk.m_Total.m_Size,
        {0x0100C0000CEEA000, "sdk", id}};
    return locateSmSession(image, memory, session);
}
} // 名前空間 yw1::logging
