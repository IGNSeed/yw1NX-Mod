#pragma once

#include <cstdint>
#include <span>

namespace yw1::companion {
struct ModuleRecord;
struct Observation;
struct Report;

struct TargetRegion {
    std::uint64_t base = 0, size = 0;
    std::uint32_t type = 0, attributes = 0, permission = 0;
    std::uint32_t ipcRefcount = 0, deviceRefcount = 0;
};

struct TargetMemoryObservation {
    std::uint64_t addressSpaceBase = 0, addressSpaceSize = 0;
    std::uint32_t addressBaseResult = UINT32_MAX, addressSizeResult = UINT32_MAX;
    std::uint32_t validationResult = UINT32_MAX, queryResult = UINT32_MAX;
    bool queryAttempted = false, infoReturned = false, regionValid = false;
    bool typeKnown = false, attributesKnown = false, permissionKnown = false;
    int containsPointer = -1, mapped = -1, readable = -1, writable = -1, executable = -1;
    TargetRegion region{};
};

struct RelativeAddress {
    bool negative;
    std::uint64_t magnitude;
};

// 符号付き整数への変換やunsigned加算を使わず、前方・後方の差分を表す。
RelativeAddress relativeAddress(std::uint64_t address, std::uint64_t base);
bool validTargetModules(std::span<const ModuleRecord> modules);
bool validTargetAddress(std::uint64_t address, std::uint64_t base, std::uint64_t size);
void validateTargetRegion(std::uint64_t address, TargetMemoryObservation& output);
const char* targetMemoryTypeName(std::uint32_t type);
const char* targetBoolean(int value);
void appendTargetMemoryReport(Report& report, const Observation& observation);
} // 名前空間 yw1::companion
