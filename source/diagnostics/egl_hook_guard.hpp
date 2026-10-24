#pragma once
#include "game/symbols/symbol_registry.hpp"
#include <array>
#include <span>

namespace yw1::diagnostics {
inline constexpr game::BuildId kSubsdk0Id{0x80, 0xBB, 0x1D, 0xEB, 0x3E, 0x83, 0xB3,
                                          0x73, 0xDC, 0xB7, 0x81, 0x2D, 0x27, 0xFF,
                                          0x21, 0xFC, 0xC7, 0xB8, 0x03, 0x5B};
inline constexpr std::array<std::uint32_t, 5> kSwapPrefix{0xA9BE4FF4, 0xA9017BFD, 0x910043FD,
                                                          0xAA0103F3, 0xAA0003F4};

enum class HookGuardStatus : std::uint32_t {
    Passed = 0,
    WrongIdentity = 1,
    InvalidRange = 2,
    InvalidInstructions = 3,
    UnsafeInstaller = 4,
    NotAttempted = 0xFFFFFFFFu
};

inline std::optional<std::uint64_t> resolveSwap(const game::ModuleImage &module) {
    constexpr game::VersionProfile profiles[]{{0x0100C0000CEEA000, "1.4.0", "subsdk0", kSubsdk0Id}};
    const game::VersionManager versions(profiles);
    const auto matched = versions.match(module.identity);
    if (!matched) {
        return std::nullopt;
    }
    constexpr game::SymbolRecord symbols[]{
        {"eglSwapBuffers", "subsdk0", kSubsdk0Id, 0x10B60, 72, 4, game::EvidenceStatus::Verified}};
    return game::SymbolRegistry(symbols).resolve("eglSwapBuffers", *matched, module);
}

inline HookGuardStatus checkSwap(const game::ModuleImage &module, std::uint64_t expectedOffset,
                                 std::uint64_t executableSize,
                                 std::span<const std::uint32_t> code) {
    const auto address = resolveSwap(module);
    if (!address) {
        return HookGuardStatus::WrongIdentity;
    }
    if (expectedOffset != 0x10B60 || executableSize > module.size || executableSize < 0x10BA8 ||
        code.size() != kSwapPrefix.size()) {
        return HookGuardStatus::InvalidRange;
    }
    for (std::size_t i = 0; i < code.size(); ++i) {
        if (code[i] != kSwapPrefix[i]) {
            return HookGuardStatus::InvalidInstructions;
        }
    }
    return HookGuardStatus::Passed;
}

// 固定版が実際に使う保守的な距離判定。負側も同じ絶対値の境界でlongとなる。
inline bool upstreamShortBranch(std::uint64_t source, std::uint64_t destination) {
    if ((source | destination) % 4 != 0) {
        return false;
    }
    const auto distance = source > destination ? source - destination : destination - source;
    return distance / 4 < (0x03FFFFFFu >> 1);
}
} // 名前空間 yw1::diagnostics
