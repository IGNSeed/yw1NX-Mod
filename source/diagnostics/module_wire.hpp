#pragma once

#include <cstddef>
#include <cstdint>

namespace yw1::diagnostics {

constexpr std::uint64_t kIdentityMagic = 0x3544494D315759ULL;
constexpr std::uint64_t kStateMagic = 0x3554534D315759ULL;
constexpr std::uint64_t kTitleId = 0x0100C0000CEEA000;
constexpr std::uint64_t kBuildTag = 0xB0015;
constexpr std::uint32_t kAbiVersion = 1;
constexpr std::size_t kStageCount = 64;
constexpr std::uint64_t kPresent = 1ULL << 32;
constexpr std::uint8_t kMainBuildId[32]{0x7F, 0x35, 0xBD, 0xFC, 0x5D, 0xE4, 0x6C, 0xF1, 0xED, 0xE9,
                                        0xC0, 0x61, 0x8B, 0x47, 0x04, 0x7B, 0xAF, 0xF4, 0x9E, 0x12};

struct ModuleIdentity {
    std::uint64_t magic = kIdentityMagic;
    std::uint32_t version = kAbiVersion;
    std::uint32_t size = 64;
    std::uint64_t titleId = kTitleId;
    std::uint64_t buildTag = kBuildTag;
    std::uint8_t mainBuildId[32]{};
};

// 各wordの観測値。複数field全体の同時snapshotとは扱わない。
struct alignas(64) ModuleSnapshot {
    std::uint64_t magic;
    std::uint32_t version;
    std::uint32_t size;
    std::uint64_t titleId;
    std::uint64_t buildTag;
    std::uint64_t latest;
    std::uint64_t firstNonzero;
    std::uint64_t lastNonzero;
    std::uint64_t observedTitleId;
    std::uint32_t directoryIndex;
    std::uint32_t ipcQuarantined;
    std::uint64_t results[kStageCount];
    std::uint64_t firstNonzeroByStage[kStageCount];
};

static_assert(sizeof(ModuleIdentity) == 64);
static_assert(sizeof(ModuleSnapshot) == 1152);
static_assert(offsetof(ModuleSnapshot, results) == 72);

inline bool contains(std::uint64_t base, std::uint64_t size, std::uint64_t address,
                     std::uint64_t length) {
    return size != 0 && base <= UINT64_MAX - size && length != 0 && address >= base &&
           address - base <= size && length <= size - (address - base);
}

inline bool validIdentity(const ModuleIdentity& identity) {
    if (identity.magic != kIdentityMagic || identity.version != kAbiVersion ||
        identity.size != sizeof(ModuleIdentity) || identity.titleId != kTitleId ||
        identity.buildTag != kBuildTag) {
        return false;
    }
    for (std::size_t i = 0; i < 32; ++i) {
        if (identity.mainBuildId[i] != kMainBuildId[i]) {
            return false;
        }
    }
    return true;
}

inline bool validSnapshot(const ModuleSnapshot& state) {
    if (!(state.magic == kStateMagic && state.version == kAbiVersion &&
          state.size == sizeof(ModuleSnapshot) && state.titleId == kTitleId &&
          state.buildTag == kBuildTag && state.ipcQuarantined <= 1 &&
          (state.latest >> 32) < kStageCount && (state.firstNonzero >> 32) < kStageCount &&
          (state.lastNonzero >> 32) < kStageCount)) {
        return false;
    }
    for (std::size_t i = 0; i < kStageCount; ++i) {
        if ((state.results[i] != 0 && state.results[i] >> 32 != 1) ||
            (state.firstNonzeroByStage[i] != 0 && state.firstNonzeroByStage[i] >> 32 != 1)) {
            return false;
        }
    }
    return true;
}

} // 名前空間 yw1::diagnostics
