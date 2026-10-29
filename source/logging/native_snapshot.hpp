#pragma once

#include "memory_log.hpp"
#include <array>
#include <span>

namespace yw1::logging {

constexpr std::size_t kSnapshotSlotSize = 4096;
constexpr std::size_t kSnapshotFooterSize = 256;
constexpr std::size_t kSnapshotPayloadSize = kSnapshotSlotSize - kSnapshotFooterSize;
constexpr std::size_t kSnapshotFileSize = 2 * kSnapshotSlotSize;
constexpr Result kForeignLog = 0xE101;
constexpr Result kSnapshotInvalid = 0xE102;
constexpr Result kShortRead = 0xE103;
constexpr Result kSequenceLimit = 0xE104;
constexpr Result kNotRun = UINT32_MAX;

enum class SnapshotField : unsigned {
    RuntimeMode, HookStatus, CallbackCount, OriginalCalls, OriginalReturns, OriginalResult,
    HookError, Reentry, InFlight, GlVerified, GlRejected, Context, ContextChanges,
    ImGuiInitialized, BackendInitialized, NewFrames, Renders, Draws, StateSaves,
    StateRestores, StateFailures, ArenaCapacity, ArenaUsed, ArenaPeak, LargestFree,
    AllocationFailures, InvalidFrees, WorkerState, WorkerCreate, WorkerStart, WorkerHandleClose,
    SmConnect, FsInitialize, SdOpen, FileOpen, FileWrite, FileFlush, FileRead, LastError,
    DroppedSnapshots, LastVerifiedSequence, Tick, MemoryDropped, SmBorrow, FspGetService,
    TransportResult, HookInstallResult, LastRenderResult, RendererDisabled, Count
};

struct NativeSnapshot {
    std::uint64_t bootId = 0;
    std::array<std::uint64_t, static_cast<unsigned>(SnapshotField::Count)> values{};
    std::uint64_t& operator[](SnapshotField field) { return values[static_cast<unsigned>(field)]; }
    std::uint64_t operator[](SnapshotField field) const {
        return values[static_cast<unsigned>(field)];
    }
};

struct SnapshotRecord {
    bool recognized = false;
    bool committed = false;
    std::uint64_t sequence = 0;
    std::uint64_t checksum = 0;
};

bool formatSnapshot(std::span<char, kSnapshotSlotSize> bytes, const NativeSnapshot& snapshot,
                    std::uint64_t sequence);
void commitSnapshot(std::span<char, kSnapshotSlotSize> bytes);
SnapshotRecord inspectSnapshot(std::span<const char, kSnapshotSlotSize> bytes);

// Render側が通知だけを行い、FS開始と継続処理は一つのWorkerが所有する。
class NativeLoggerGate {
  public:
    bool afterSwap(std::uint32_t result) {
        return result != 0 && !started_.exchange(true, std::memory_order_acq_rel);
    }
  private:
    std::atomic<bool> started_{false};
};

class SnapshotSchedule {
  public:
    bool due(std::uint64_t now, std::uint64_t important) const {
        return !saved_ || now < lastTime_ || now - lastTime_ >= 10 * 19200000ULL ||
               important != lastImportant_;
    }
    void saved(std::uint64_t now, std::uint64_t important) {
        saved_ = true; lastTime_ = now; lastImportant_ = important;
    }
  private:
    bool saved_ = false;
    std::uint64_t lastTime_ = 0, lastImportant_ = 0;
};

// 同期SVCを取消済みと偽装せず、期限超過後はWorkerと静的IPC領域を永久に隔離する。
class NativeIoDeadline {
  public:
    bool begin(std::uint64_t now) {
        if (quarantined() || now >= UINT64_MAX - 19200001ULL || last_ == UINT64_MAX - 1) {
            return false;
        }
        auto next = now + 19200000ULL;
        if (next <= last_) { next = last_ + 1; }
        last_ = next;
        deadline_.store(next, std::memory_order_release);
        return true;
    }
    bool expire(std::uint64_t now) {
        auto value = deadline_.load(std::memory_order_acquire);
        return value != 0 && value != UINT64_MAX && now >= value &&
            deadline_.compare_exchange_strong(value, UINT64_MAX, std::memory_order_acq_rel);
    }
    bool complete(std::uint64_t now) {
        auto value = deadline_.load(std::memory_order_acquire);
        if (value == 0 || value == UINT64_MAX) { return false; }
        const auto replacement = now >= value ? UINT64_MAX : 0;
        return deadline_.compare_exchange_strong(value, replacement, std::memory_order_acq_rel) &&
               replacement == 0;
    }
    bool quarantined() const { return deadline_.load(std::memory_order_acquire) == UINT64_MAX; }
  private:
    std::atomic<std::uint64_t> deadline_{0};
    std::uint64_t last_ = 0;
};

} // 名前空間 yw1::logging
