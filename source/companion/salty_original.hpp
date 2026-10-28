#pragma once

#include "target_memory.hpp"

#include <array>
#include <span>

namespace yw1::companion {
struct GotObservation;
struct ModuleRecord;
struct Observation;
struct Report;
constexpr std::uint64_t kSaltySwapOffset = 0x1E80;
constexpr std::uint64_t kOriginalStorageOffset = 0x2A3B8;
constexpr std::uint64_t kSaltyRwOffset = 0x22000;
constexpr std::uint64_t kSaltyRwSize = 0x16000;
constexpr std::uint32_t kOriginalIdentityMismatch = 0xE10C;
constexpr std::uint32_t kOriginalStorageUnsupported = 0xE10D;

// 固定公開参照の照合digest。命令byteや実機ASLR addressは埋め込まない。
constexpr std::array<std::uint8_t, 32> kSaltySwapDigest{
    0xDE, 0xE1, 0x95, 0x9B, 0xAF, 0xAF, 0xB6, 0x5B, 0xBF, 0xF3, 0x11, 0xB6, 0xC2, 0xBF, 0xDC, 0x3C,
    0x03, 0x66, 0x66, 0x2C, 0x3B, 0xC8, 0x53, 0x9D, 0x8C, 0x1A, 0x0F, 0x9B, 0x10, 0xBB, 0x32, 0xD7};

enum class OriginalPointerKind : std::uint32_t {
    Unknown,
    Null,
    Unaligned,
    OutsideAddressSpace,
    SwapSelf,
    CoreInternal,
    InvalidRegion,
    NonExecutable,
    ExpectedSubsdk0Export,
    LoadedModule,
    OtherExecutable,
    AmbiguousModule
};

struct OriginalEglObservation {
    std::uint32_t result = UINT32_MAX, identity = UINT32_MAX, preflight = UINT32_MAX;
    std::uint32_t beforeValidation = UINT32_MAX, beforeStorage = UINT32_MAX;
    std::uint32_t map = UINT32_MAX, read = UINT32_MAX, unmap = UINT32_MAX;
    std::uint32_t afterStorage = UINT32_MAX, postvalidation = UINT32_MAX;
    std::uint32_t pointerQuery = UINT32_MAX;
    bool coreKnown = false, storageQueryAttempted = false, storageInfoReturned = false;
    bool mapAttempted = false, readAttempted = false, unmapAttempted = false;
    bool readsMatched = false, snapshotValid = false, readerQuarantined = false;
    bool pointerQueryAttempted = false, pointerInfoReturned = false, moduleKnown = false;
    std::uint64_t coreBase = 0, storageAddress = 0, pointer = 0, moduleOffset = 0;
    std::uint64_t pageAddress = 0;
    std::uint32_t pageOffset = 0;
    int executable = -1, expectedExport = -1;
    TargetMemoryObservation storage{}, target{};
    OriginalPointerKind kind = OriginalPointerKind::Unknown;
    int moduleIndex = -1;
    const char* moduleName = "unknown";
    std::array<std::uint8_t, 32> moduleId{};
};

std::uint32_t deriveOriginalStorage(const GotObservation& got,
                                  std::span<const std::uint8_t, 32> digest,
                                  OriginalEglObservation& output);
std::uint32_t originalStoragePreflight(const GotObservation& got,
                                      OriginalEglObservation& output);
void classifyOriginalPointer(std::span<const ModuleRecord> modules, const GotObservation& got,
                             OriginalEglObservation& output);
const char* originalPointerKindName(OriginalPointerKind kind);
void appendOriginalReport(Report& report, const Observation& observation);
} // 名前空間 yw1::companion
