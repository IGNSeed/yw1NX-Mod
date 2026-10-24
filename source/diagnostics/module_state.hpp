#pragma once

#include "module_wire.hpp"

#include <atomic>

namespace yw1::diagnostics {

struct alignas(64) ModuleDiagnostics {
    std::uint64_t magic = kStateMagic;
    std::uint32_t version = kAbiVersion;
    std::uint32_t size = sizeof(ModuleSnapshot);
    std::uint64_t titleId = kTitleId;
    std::uint64_t buildTag = kBuildTag;
    std::atomic<std::uint64_t> latest{0};
    std::atomic<std::uint64_t> firstNonzero{0};
    std::atomic<std::uint64_t> lastNonzero{0};
    std::atomic<std::uint64_t> observedTitleId{0};
    std::atomic<std::uint32_t> directoryIndex{UINT32_MAX};
    std::atomic<std::uint32_t> ipcQuarantined{0};
    std::atomic<std::uint64_t> results[kStageCount]{};
    std::atomic<std::uint64_t> firstNonzeroByStage[kStageCount]{};
    std::atomic<std::uint64_t> smConnectAttempts{0};
    std::atomic<std::uint64_t> writerTerminal{0};
    std::atomic<std::uint64_t> writerPolicy{0};
};

static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
static_assert(sizeof(ModuleDiagnostics) == sizeof(ModuleSnapshot));
static_assert(offsetof(ModuleDiagnostics, results) == offsetof(ModuleSnapshot, results));

extern "C" {
extern const ModuleIdentity yw1_module_identity;
extern ModuleDiagnostics yw1_module_diagnostics_state;
}

void recordModuleProgress(std::uint32_t stage, std::uint32_t result);

} // 名前空間 yw1::diagnostics
