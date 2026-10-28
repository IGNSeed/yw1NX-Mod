#pragma once
#include "egl_hook_guard.hpp"
#include "game/symbols/symbol_registry.hpp"

namespace yw1::diagnostics {
inline constexpr game::BuildId kContextSdkId{0x86, 0xA2, 0xAF, 0xD8, 0x63, 0xC9, 0x90,
    0x62, 0xD6, 0x66, 0x22, 0x6A, 0x9E, 0x7A, 0xA7, 0x64, 0xB6, 0x53, 0xEC, 0xEC};
struct ContextBinding {
    game::SymbolRecord symbol;
    std::array<std::uint32_t, 21> instructions;
};
// B029の実IDAで関数境界・公開ABI・命令全体を照合した固定対象だけを許可する。
inline constexpr ContextBinding kContextBindings[]{
    {{"eglQueryAPI", "subsdk0", kSubsdk0Id, 0x10850, 76, 4,
      game::EvidenceStatus::Verified}, {
        0xF81E0FF3, 0xA9017BFD, 0x910043FD, 0x320003E1, 0x2A1F03E0,
        0x97FFC4CB, 0xB4000140, 0xF9400408, 0xAA0003F3, 0x91006100,
        0x320003E1, 0x9400A859, 0xF9408260, 0xA9417BFD, 0xF84207F3,
        0xD61F0000, 0xA9417BFD, 0xF84207F3, 0xD65F03C0,
    }},
    {{"eglGetCurrentContext", "subsdk0", kSubsdk0Id, 0x10620, 44, 4,
      game::EvidenceStatus::Verified}, {
        0xA9BF7BFD, 0x910003FD, 0x2A1F03E0, 0x2A1F03E1, 0x97FFC558,
        0xB4000080, 0xF9406000, 0xA8C17BFD, 0xD61F0000, 0xA8C17BFD,
        0xD65F03C0,
    }},
    {{"eglGetCurrentDisplay", "subsdk0", kSubsdk0Id, 0x10650, 44, 4,
      game::EvidenceStatus::Verified}, {
        0xA9BF7BFD, 0x910003FD, 0x2A1F03E0, 0x2A1F03E1, 0x97FFC54C,
        0xB4000080, 0xF9406400, 0xA8C17BFD, 0xD61F0000, 0xA8C17BFD,
        0xD65F03C0,
    }},
    {{"eglGetCurrentSurface", "subsdk0", kSubsdk0Id, 0x10680, 64, 4,
      game::EvidenceStatus::Verified}, {
        0xF81E0FF3, 0xA9017BFD, 0x910043FD, 0x2A0003F3, 0x2A1F03E0,
        0x2A1F03E1, 0x97FFC53E, 0xB40000C0, 0xF9406801, 0xA9417BFD,
        0x2A1303E0, 0xF84207F3, 0xD61F0020, 0xA9417BFD, 0xF84207F3,
        0xD65F03C0,
    }},
    {{"eglGetProcAddress", "subsdk0", kSubsdk0Id, 0x10720, 84, 4,
      game::EvidenceStatus::Verified}, {
        0xA9BE4FF4, 0xA9017BFD, 0x910043FD, 0xAA0003F3, 0x320003E1,
        0x2A1F03E0, 0x97FFC516, 0xB4000160, 0xF9400408, 0xAA0003F4,
        0x91006100, 0x320003E1, 0x9400A8A4, 0xF9407681, 0xA9417BFD,
        0xAA1303E0, 0xA8C24FF4, 0xD61F0020, 0xA9417BFD, 0xA8C24FF4,
        0xD65F03C0,
    }},
    {{"nn::os::GetTlsValue", "sdk", kContextSdkId, 0xc4ae0, 20, 4,
      game::EvidenceStatus::Verified}, {
        0xD53BD068, 0xF940FD08, 0x8B204D08, 0xF9404100, 0xD65F03C0,
    }},
    {{"_glGetString", "subsdk0", kSubsdk0Id, 0x986240, 68, 4,
      game::EvidenceStatus::Verified}, {
        0xF81E0FF3, 0xA9017BFD, 0x910043FD, 0xB00085A8, 0xF9439108,
        0xF9400108, 0x2A0003F3, 0xF9400500, 0xD0008588, 0xF9422108,
        0xF9406908, 0xD63F0100, 0xF9439001, 0xA9417BFD, 0x2A1303E0,
        0xF84207F3, 0xD61F0020,
    }},
    {{"_glGetIntegerv", "subsdk0", kSubsdk0Id, 0x985e10, 76, 4,
      game::EvidenceStatus::Verified}, {
        0xA9BE4FF4, 0xA9017BFD, 0x910043FD, 0xD00085A8, 0xF9439108,
        0xF9400108, 0x2A0003F4, 0xF9400500, 0xF0008588, 0xF9422108,
        0xF9406908, 0xAA0103F3, 0xD63F0100, 0xF9436002, 0xA9417BFD,
        0x2A1403E0, 0xAA1303E1, 0xA8C24FF4, 0xD61F0040,
    }},
};
inline std::optional<std::uint64_t> resolveContextBinding(unsigned index,
                                                         const game::ModuleImage& module) {
    if (index >= std::size(kContextBindings)) { return std::nullopt; }
    const auto& symbol = kContextBindings[index].symbol;
    const game::VersionProfile profile{0x0100C0000CEEA000, "1.4.0", symbol.moduleName,
                                        symbol.fullBuildId};
    const auto matched = game::VersionManager(std::span(&profile, 1)).match(module.identity);
    return matched ? game::SymbolRegistry(std::span(&symbol, 1)).resolve(symbol.name, *matched,
                                                                        module) : std::nullopt;
}
} // 名前空間 yw1::diagnostics
