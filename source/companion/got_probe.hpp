#pragma once

#include <array>
#include <cstdint>
#include <span>
#if YW1_COMPANION_EGL_TARGET_MEMORY
#include "target_memory.hpp"
#endif
#if YW1_COMPANION_EGL_CODE_SNAPSHOT
#include "code_snapshot.hpp"
#endif

namespace yw1::companion {
struct ModuleRecord;
constexpr std::uint64_t kGotSlotOffset = 0x8B4398;
constexpr std::uint64_t kInitialPltOffset = 0x65DB90;
enum class GotTarget : std::uint32_t {
    Unknown,
    Null,
    Main,
    Subsdk0,
    Sdk,
    Rtld,
    Subsdk1,
    Bootstrap,
    OtherLoaded,
    Outside,
    Ambiguous
};
enum class GotBinding : std::uint32_t {
    Unknown,
    LazyResolverCandidate,
    ExpectedExportAddress,
    ExecutableCandidate,
    NonExecutable
};
struct GotObservation {
    std::uint32_t result = UINT32_MAX, readResult = UINT32_MAX;
    std::uint32_t pageQueryResult = UINT32_MAX, targetQueryResult = UINT32_MAX;
    bool attempted = false, mainVerified = false, valueKnown = false, normalizedKnown = false;
    std::uint64_t mainBase = 0, slotAddress = 0, slotValue = 0, targetOffset = 0;
    std::array<std::uint8_t, 32> targetId{};
    int targetIndex = -1, matchesExpected = -1, executable = -1;
    std::uint32_t pagePermission = UINT32_MAX, targetPermission = UINT32_MAX;
    GotTarget target = GotTarget::Unknown;
    GotBinding binding = GotBinding::Unknown;
#if YW1_COMPANION_EGL_TARGET_MEMORY
    TargetMemoryObservation memory{};
#endif
#if YW1_COMPANION_EGL_CODE_SNAPSHOT
    CodeSnapshot snapshot{};
#endif
};

// 完全ID・module・symbol・page全体の境界を確認する。ここではメモリを読まない。
std::uint32_t validateGotRead(std::uint64_t pid, std::uint64_t title,
                              std::span<const ModuleRecord> modules, bool complete,
                              GotObservation& output, std::uint64_t offset = kGotSlotOffset);
void classifyGot(std::span<const ModuleRecord> modules, GotObservation& output);
bool sameNormalizedGot(const GotObservation& left, const GotObservation& right);
const char* gotTargetName(GotTarget target);
const char* gotBindingName(GotBinding binding);
} // 名前空間 yw1::companion
