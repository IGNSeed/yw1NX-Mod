#pragma once
#include "context_bindings.hpp"

namespace yw1::diagnostics {
// 実測pointer・実IDAの登録名・slot・末尾RETまでの命令に対応する2入口だけを許可する。
inline constexpr ContextBinding kGlGetterWhitelist[]{
    {{"glGetString dispatch stub", "subsdk0", kSubsdk0Id, 0x19080, 28, 4,
      game::EvidenceStatus::Verified}, {
        0xF000D0E8, 0xF940C108, 0xF94E6901, 0xB4000041, 0xD61F0020,
        0xAA1F03E0, 0xD65F03C0}},
    {{"glGetIntegerv dispatch stub", "subsdk0", kSubsdk0Id, 0x17C60, 24, 4,
      game::EvidenceStatus::Verified}, {
        0xB000D108, 0xF940C108, 0xF94BE502, 0xB4000042, 0xD61F0040,
        0xD65F03C0}}
};
inline constexpr std::uint64_t kGlGetterTablePointer = 0x1A38180;
inline constexpr std::uint64_t kGlGetterTable = 0x1A409E8;
inline constexpr std::array<std::uint64_t, 2> kGlGetterSlots{0x1A426B8, 0x1A421B0};

inline bool verifyGlGetter(unsigned index, const game::ModuleImage& module,
                           std::uintptr_t pointer, std::span<const std::uint32_t> code) {
    if (index >= std::size(kGlGetterWhitelist)) { return false; }
    const auto& binding = kGlGetterWhitelist[index];
    const auto& symbol = binding.symbol;
    const game::VersionProfile profile{0x0100C0000CEEA000, "1.4.0", "subsdk0", kSubsdk0Id};
    const auto version = game::VersionManager(std::span(&profile, 1)).match(module.identity);
    const auto address = version ? game::SymbolRegistry(std::span(&symbol, 1)).resolve(
        symbol.name, *version, module) : std::nullopt;
    if (!address || pointer != *address || code.size() != symbol.size / 4) { return false; }
    for (unsigned i = 0; i < code.size(); ++i) {
        if (code[i] != binding.instructions[i]) { return false; }
    }
    return true;
}
} // 名前空間 yw1::diagnostics
