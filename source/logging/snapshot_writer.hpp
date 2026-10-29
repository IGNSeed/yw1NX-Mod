#pragma once

#include "ipc_fs.hpp"
#include "native_snapshot.hpp"

namespace yw1::logging {

struct SnapshotOutcome {
    Result result = kNotRun;
    Result initialize = kNotRun, sdOpen = kNotRun, fileOpen = kNotRun;
    Result write = kNotRun, flush = kNotRun, receiptWrite = kNotRun, receiptFlush = kNotRun;
    Result read = kNotRun;
    std::uint64_t verifiedSequence = 0;
};

struct SnapshotIo {
    // 非同期IPCが参照するページは、Worker終了・timeout後も再利用しない。
    alignas(4096) std::array<char, kSnapshotSlotSize> frame{};
    alignas(4096) std::array<char, kSnapshotSlotSize> readback{};
};

class SnapshotWriter {
  public:
    Result open(IpcFs& fs, SnapshotIo& io, Handle borrowedSm = 0);
    Result save(IpcFs& fs, SnapshotIo& io, const NativeSnapshot& snapshot);
    const SnapshotOutcome& outcome() const { return outcome_; }
  private:
    Result recover(IpcFs& fs, SnapshotIo& io);
    SnapshotOutcome outcome_{};
    std::uint64_t sequence_ = 0;
    unsigned slot_ = 0;
    bool entered_ = false, ready_ = false;
};

} // 名前空間 yw1::logging
