#pragma once

#include "memory_log.hpp"
#if YW1_BOOTSTRAP_EXTERNAL_DIAGNOSTICS
#include "diagnostics/module_state.hpp"
#endif

namespace yw1::logging {

enum class Stage : std::uint32_t {
    InitEnter = 1,
    ProcessHandle,
    ProgramId,
    Exlaunch,
    ExlMain,
    InitReturn,
    RwMap = 8,
    RwUnmap,
    WorkerCreate = 16,
    WorkerStart,
    WorkerHandleClose,
    WorkerEnter,
    SmConnect = 32,
    SmRegister,
    SmGetService,
    SmDetach,
    SmClose,
    FspSetProcess,
    SdOpen = 48,
    EntryType,
    CreateDirectory,
    CreateFile,
    FileOpen,
    FileSize,
    FileWrite,
    FileFlush,
    SessionClose,
    Saved,
    PriorityQuery = 60,
    GuardDenied,
    LaunchAwait
};

constexpr Result kInProgress = 0xFFFFFFFF;

struct ProgressState {
    std::atomic<std::uint64_t> latest{0};
    std::atomic<std::uint64_t> firstNonzero{0};
    std::atomic<std::uint64_t> lastNonzero{0};
    std::atomic<Result> processHandleResult{kInProgress};
    std::atomic<std::uint64_t> observedTitleId{0};
    std::atomic<Result> workerCreate{kInProgress};
    std::atomic<Result> workerStart{kInProgress};
    std::atomic<Result> workerHandleClose{kInProgress};
    std::atomic<std::uint32_t> directoryIndex{0xFFFFFFFF};
};

// 各stageとResultの組を一つのatomicで保持する。外部回収は専用build optionで有効にする。
inline constinit ProgressState progress;

inline Result recordProgress(Stage stage, Result result) {
#if YW1_BOOTSTRAP_EXTERNAL_DIAGNOSTICS
    diagnostics::recordModuleProgress(static_cast<std::uint32_t>(stage), result);
#endif
    const auto value = (static_cast<std::uint64_t>(stage) << 32) | result;
    progress.latest.store(value, std::memory_order_release);
    if (result != 0 && result != kInProgress) {
        progress.lastNonzero.store(value, std::memory_order_release);
        std::uint64_t empty = 0;
        static_cast<void>(progress.firstNonzero.compare_exchange_strong(
            empty, value, std::memory_order_release, std::memory_order_relaxed));
    }
    return result;
}

inline void recordObservedTitleId(std::uint64_t titleId) {
    progress.observedTitleId.store(titleId, std::memory_order_release);
#if YW1_BOOTSTRAP_EXTERNAL_DIAGNOSTICS
    diagnostics::yw1_module_diagnostics_state.observedTitleId.store(titleId,
                                                                    std::memory_order_release);
#endif
}

inline void recordDirectoryIndex(std::uint32_t index) {
    progress.directoryIndex.store(index, std::memory_order_release);
#if YW1_BOOTSTRAP_EXTERNAL_DIAGNOSTICS
    diagnostics::yw1_module_diagnostics_state.directoryIndex.store(index,
                                                                   std::memory_order_release);
#endif
}

} // 名前空間 yw1::logging
