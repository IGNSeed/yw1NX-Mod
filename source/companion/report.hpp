#pragma once

#include "diagnostics/log_export_wire.hpp"
#include "diagnostics/module_wire.hpp"

#include <cstdarg>
#include <cstdio>

namespace yw1::companion {

constexpr std::uint32_t kNotAttempted = UINT32_MAX;
constexpr std::uint32_t kInvalidData = 0xE101;
constexpr std::uint32_t kWrongTarget = 0xE102;
constexpr std::uint32_t kUnavailable = 0xE103;
constexpr std::uint32_t kNoAddress = 0xE104;
constexpr std::uint32_t kFileLimit = 0xE105;
constexpr std::size_t kLoaderQueryCapacity = 16;
constexpr std::uint32_t kSessionClosed = 0xF601;
constexpr std::uint32_t kExportInvalid = 0xE106;
constexpr std::uint32_t kExportChanged = 0xE107;

// 固定Loaderの0x420以内かつ、交渉されたPointer Bufferの容量に制限する。
inline std::size_t loaderCapacity(std::size_t pointerBytes) {
    const auto count = pointerBytes / 48;
    return count < kLoaderQueryCapacity ? count : kLoaderQueryCapacity;
}
constexpr std::size_t kReportCapacity = 24 * 1024;
constexpr std::uint64_t kFileCapacity = 1024 * 1024;
constexpr char kLogPath[] = "/atmosphere/contents/0100C0000CEEA000/yw1NX-MOD/logs.txt";

struct Report {
    char text[kReportCapacity]{};
    std::size_t size = 0;
    bool truncated = false;
    std::uint32_t firstAnomaly = 0;

    void append(const char* format, ...) {
        va_list args;
        va_start(args, format);
        const auto length = std::vsnprintf(text + size, sizeof(text) - size, format, args);
        va_end(args);
        if (length < 0 || static_cast<std::size_t>(length) >= sizeof(text) - size) {
            truncated = true;
            return;
        }
        size += static_cast<std::size_t>(length);
    }

    std::uint32_t result(const char* label, std::uint32_t value) {
        if (firstAnomaly == 0 && value != 0 && value != kNotAttempted) {
            firstAnomaly = value;
        }
        append("%s=0x%08X\n", label, value);
        return value;
    }
};

struct ModuleRecord {
    std::uint8_t buildId[32];
    std::uint64_t base;
    std::uint64_t size;
};
static_assert(sizeof(ModuleRecord) == 48);

inline bool equalId(const std::uint8_t* left, const std::uint8_t* right) {
    for (std::size_t i = 0; i < 32; ++i) {
        if (left[i] != right[i]) {
            return false;
        }
    }
    return true;
}

// Loaderの最大32件を超えて読まず、同一Build IDの複数候補も採用しない。
inline int findModule(const ModuleRecord* modules, std::size_t count, const std::uint8_t* buildId) {
    if (modules == nullptr || buildId == nullptr || count > 32) {
        return -2;
    }
    int found = -1;
    for (std::size_t i = 0; i < count; ++i) {
        if (equalId(modules[i].buildId, buildId)) {
            if (found != -1 || modules[i].base == 0 || modules[i].base % 4096 != 0 ||
                !diagnostics::contains(modules[i].base, modules[i].size, modules[i].base,
                                       modules[i].size)) {
                return -2;
            }
            found = static_cast<int>(i);
        }
    }
    return found;
}

inline bool validRead(const ModuleRecord& module, std::uint64_t offset, std::size_t size) {
    return module.base != 0 && module.base % 4096 == 0 && module.base <= UINT64_MAX - offset &&
           size % 8 == 0 && offset % 8 == 0 &&
           diagnostics::contains(module.base, module.size, module.base + offset, size) &&
           size <= 4096 && (module.base + offset) % 4096 <= 4096 - size;
}

struct Observation {
    std::uint64_t pid = 0;
    int moduleCount = -1;
    int moduleDetected = -1;
    int dtInitReached = -1;
    std::uint32_t result = kNotAttempted;
    bool snapshotRead = false;
    bool cleanupFailed = false;
    diagnostics::ModuleSnapshot snapshot{};
};

struct BootstrapLog {
    diagnostics::ExportedLogHeader header{};
    alignas(8) char text[diagnostics::kExportCapacity]{};
    std::uint64_t pid = 0;
    std::uint64_t moduleBase = 0;
    std::uint32_t readResult = kNotAttempted;
    bool duplicate = false;
};

Observation inspectTarget(Report& report, BootstrapLog* bootstrap = nullptr);

struct FileOutcome {
    std::uint32_t sdOpen = kNotAttempted;
    std::uint32_t directory = kNotAttempted;
    std::uint32_t fileOpen = kNotAttempted;
    std::uint32_t prepare = kNotAttempted;
    std::uint32_t write = kNotAttempted;
    std::uint32_t flush = kNotAttempted;
    std::uint32_t receiptWrite = kNotAttempted;
    std::uint32_t receiptFlush = kNotAttempted;
    bool closeIssued = false;
    std::size_t savedThrough = 0;
};

FileOutcome saveReport(Report& report, std::size_t startOffset = 0);
std::uint32_t saveBootstrapLog(Report& report, BootstrapLog& bootstrap);

} // 名前空間 yw1::companion
