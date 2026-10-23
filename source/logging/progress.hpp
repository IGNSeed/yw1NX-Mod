#pragma once

#include "memory_log.hpp"

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
    Saved
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

// 回収方法を実装したとは扱わない。各stageとResultの組を一つのatomicで保持する。
inline constinit ProgressState progress;

inline Result recordProgress(Stage stage, Result result) {
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

} // 名前空間 yw1::logging
