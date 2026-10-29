#pragma once

#include "native_snapshot.hpp"

namespace yw1::logging {

enum class NativeWorkerState : std::uint32_t {
    Dormant, Creating, Starting, Waiting, Connecting, Running, Saving, Stopped, Quarantined
};

struct NativeLoggerStatus {
    std::atomic<NativeWorkerState> state{NativeWorkerState::Dormant};
    std::atomic<Result> create{kNotRun}, start{kNotRun}, handleClose{kNotRun}, lastError{0};
    std::atomic<Result> smBorrow{kNotRun}, transport{kNotRun};
    std::atomic<std::uint64_t> verifiedSequence{0}, dropped{0};
};

NativeLoggerStatus& nativeLoggerStatus();
void notifyNativeLoggerAfterSwap(std::uint32_t result);

} // 名前空間 yw1::logging
