#pragma once

#include <array>
#include <cstdint>

namespace yw1::companion {
struct GotObservation;
struct TargetRegion;
struct Report;
struct Observation;
constexpr std::uint32_t kSnapshotChanged = 0xE109;
constexpr std::uint32_t kSnapshotUnsupported = 0xE10A;
constexpr std::uint32_t kSnapshotMismatch = 0xE10B;
#if YW1_COMPANION_EGL_CODE_ATTRIBUTION
constexpr std::uint32_t kSnapshotLength = 64;
#else
constexpr std::uint32_t kSnapshotLength = 16;
#endif
static_assert(kSnapshotLength <= 64 && kSnapshotLength % 4 == 0);

struct CodeSnapshot {
    std::uint32_t result = UINT32_MAX, preflight = UINT32_MAX, revalidation = UINT32_MAX;
    std::uint32_t map = UINT32_MAX, read = UINT32_MAX, unmap = UINT32_MAX;
    std::uint32_t postvalidation = UINT32_MAX;
    std::uint32_t beforePid = UINT32_MAX, beforeTitle = UINT32_MAX;
    std::uint32_t beforeGot = UINT32_MAX, beforeQuery = UINT32_MAX;
    std::uint32_t afterPid = UINT32_MAX, afterTitle = UINT32_MAX;
    std::uint32_t afterGot = UINT32_MAX, afterQuery = UINT32_MAX;
#if YW1_COMPANION_EGL_CODE_ATTRIBUTION
    std::uint32_t beforeMain = UINT32_MAX, afterMain = UINT32_MAX;
#endif
    bool preflightAttempted = false, mapAttempted = false, readAttempted = false;
    bool unmapAttempted = false, cleanupQuarantined = false, bytesValid = false;
    bool readsMatched = false;
    std::uint64_t pageAddress = 0;
    std::uint32_t pageOffset = 0;
    std::array<std::uint8_t, kSnapshotLength> bytes{};
};

std::uint32_t snapshotPreflight(const GotObservation& got, CodeSnapshot& output);
bool sameSnapshotRegion(const TargetRegion& left, const TargetRegion& right);
const char* snapshotConsistency(const CodeSnapshot& snapshot);
void appendSnapshotReport(Report& report, const Observation& observation);
} // 名前空間 yw1::companion
