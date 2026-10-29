#include "native_logger.hpp"
#include "snapshot_writer.hpp"
#include "sm_session.hpp"
#include "diagnostics/callback_wire.hpp"
#include "diagnostics/render_wire.hpp"
#include "diagnostics/log_export.hpp"
#include <common.hpp>

namespace yw1::logging {
namespace {
using S = SnapshotField;
using R = diagnostics::RenderField;
using C = diagnostics::CallbackField;
constexpr std::uint64_t kOperationTicks = 8 * 19200000ULL;
constinit NativeLoggerGate gate;
constinit NativeLoggerStatus status;
constinit IpcStorage ipc;
constinit SnapshotIo io;
constinit SnapshotWriter writer;
constinit NativeIoDeadline operation;
constinit std::atomic<bool> launchComplete{false};
alignas(4096) unsigned char stack[64 * 1024];

Result close(void*, Handle handle) { return svcCloseHandle(handle); }
std::uint64_t ticks(void*) { return svcGetSystemTick(); }
void sleep(void*, std::uint64_t ns) { svcSleepThread(static_cast<std::int64_t>(ns)); }
Result sendSync(void*, void* data, std::size_t size, Handle session) {
    if (!operation.begin(svcGetSystemTick())) { return kDeadline; }
    const auto result = svcSendSyncRequestWithUserBuffer(data, size, session);
    status.transport.store(result, std::memory_order_release);
    // SVCの復帰前に期限超過しても、取消完了・Session再利用とは扱わない。
    return operation.complete(svcGetSystemTick()) ? result : kDeadline;
}
bool abandoned(void*) { return operation.quarantined(); }

bool snapshot(NativeSnapshot& out, const IpcFs& fs) {
    // Render側を待たせず、偶数の同一generationだけを最大3回で受け取る。
    diagnostics::RenderWire render;
    bool acquired = false;
    auto& published = diagnostics::yw1_render_diagnostics;
    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        const auto before = std::atomic_ref(published.generation).load(std::memory_order_acquire);
        if (before & 1) { continue; }
        for (unsigned i = 0; i < render.words.size(); ++i) {
            render.words[i] = std::atomic_ref(published.words[i]).load(std::memory_order_acquire);
        }
        const auto after = std::atomic_ref(published.generation).load(std::memory_order_acquire);
        if (before == after && !(after & 1)) { acquired = true; break; }
    }
    if (!acquired) { return false; }
    out.bootId = diagnostics::yw1_module_log_header.bootToken.load(std::memory_order_acquire);
    const auto callback = [](C field) {
        return diagnostics::yw1_callback_diagnostics.words[static_cast<unsigned>(field)]
            .load(std::memory_order_acquire);
    };
    out[S::OriginalReturns] = callback(C::OriginalReturns);
    out[S::OriginalCalls] = callback(C::OriginalCalls);
    out[S::CallbackCount] = callback(C::CallbackCount);
    out[S::HookStatus] = callback(C::HookActive);
    out[S::OriginalResult] = callback(C::LastOriginalResult);
    out[S::HookError] = callback(C::HookError);
    out[S::Reentry] = callback(C::ReentryCount);
    out[S::InFlight] = callback(C::InFlight);
    out[S::RuntimeMode] = render[R::Mode];
    out[S::GlVerified] = render[R::GlVerified]; out[S::GlRejected] = render[R::GlRejected];
    out[S::Context] = render[R::Context]; out[S::ContextChanges] = render[R::ContextChanges];
    out[S::ImGuiInitialized] = render[R::ContextLive];
    out[S::BackendInitialized] = render[R::BackendInitialized];
    out[S::NewFrames] = render[R::NewFrames]; out[S::Renders] = render[R::RenderCalls];
    out[S::Draws] = render[R::DrawSubmitted]; out[S::StateSaves] = render[R::StateSaves];
    out[S::StateRestores] = render[R::StateRestores]; out[S::StateFailures] = render[R::StateFailures];
    out[S::ArenaCapacity] = render[R::ArenaCapacity]; out[S::ArenaUsed] = render[R::ArenaUsed];
    out[S::ArenaPeak] = render[R::ArenaPeak]; out[S::LargestFree] = render[R::ArenaLargestFree];
    out[S::AllocationFailures] = render[R::ArenaFailures];
    out[S::InvalidFrees] = render[R::ArenaInvalidFrees];
    out[S::WorkerState] = static_cast<unsigned>(status.state.load(std::memory_order_acquire));
    out[S::WorkerCreate] = status.create.load(std::memory_order_acquire);
    out[S::WorkerStart] = status.start.load(std::memory_order_acquire);
    out[S::WorkerHandleClose] = status.handleClose.load(std::memory_order_acquire);
    const auto& previous = writer.outcome();
    out[S::SmConnect] = fs.connectResult(); out[S::FsInitialize] = previous.initialize;
    out[S::SdOpen] = previous.sdOpen; out[S::FileOpen] = previous.fileOpen;
    out[S::FileWrite] = previous.write; out[S::FileFlush] = previous.flush;
    out[S::FileRead] = previous.read;
    out[S::LastError] = status.lastError.load(std::memory_order_acquire);
    out[S::DroppedSnapshots] = status.dropped.load(std::memory_order_acquire);
    out[S::LastVerifiedSequence] = previous.verifiedSequence;
    out[S::Tick] = svcGetSystemTick(); out[S::MemoryDropped] = memoryLog().dropped();
    out[S::SmBorrow] = status.smBorrow.load(std::memory_order_acquire);
    out[S::FspGetService] = fs.serviceResult();
    out[S::TransportResult] = status.transport.load(std::memory_order_acquire);
    out[S::HookInstallResult] = callback(C::InstallResult);
    out[S::LastRenderResult] = render[R::LastRenderResult];
    out[S::RendererDisabled] = render[R::RendererDisabled];
    return out.bootId != 0 && out[S::OriginalReturns] <= out[S::OriginalCalls] &&
           out[S::OriginalCalls] <= out[S::CallbackCount];
}

std::uint64_t important(const NativeSnapshot& value) {
    std::uint64_t result = 14695981039346656037ULL;
    for (auto field : {S::ContextChanges, S::ImGuiInitialized, S::BackendInitialized,
                       S::StateFailures, S::AllocationFailures, S::InvalidFrees, S::HookError,
                       S::GlRejected, S::RuntimeMode}) {
        result = (result ^ value[field]) * 1099511628211ULL;
    }
    return result;
}

[[noreturn]] void exitWorker(Result result, bool quarantined) {
    status.lastError.store(result, std::memory_order_release);
    status.state.store(quarantined ? NativeWorkerState::Quarantined : NativeWorkerState::Stopped,
                       std::memory_order_release);
    recordProgress(Stage::Saved, result);
    recordWriterTerminal(quarantined ? 2 : 1);
    constexpr char label[] = "[YW1] native_logger_terminal_result=";
    static_cast<void>(memoryLog().appendResult(label, sizeof(label) - 1, result));
    svcExitThread();
    __builtin_unreachable();
}

[[noreturn]] void worker(void*) {
    status.state.store(NativeWorkerState::Waiting, std::memory_order_release);
    const auto launchDeadline = svcGetSystemTick() + 1920000;
    while (!launchComplete.load(std::memory_order_acquire)) {
        if (svcGetSystemTick() >= launchDeadline) { exitWorker(kDeadline, false); }
        svcSleepThread(1000000);
    }
    // Raw WorkerはSDK ThreadType・SDK/libnx TLS・allocator・mount表を使わない。
    recordProgress(Stage::WorkerEnter, 0);
    const KernelApi api{nullptr, nullptr, nullptr, nullptr, close, ticks, sleep, sendSync, abandoned};
    IpcFs fs(api, ipc, svcGetSystemTick() + kOperationTicks);
    status.state.store(NativeWorkerState::Connecting, std::memory_order_release);
    Handle sm = 0;
    auto result = loadNativeSmSession(sm);
    status.smBorrow.store(result, std::memory_order_release);
    if (result == 0) { result = writer.open(fs, io, sm); }
    if (result == 0) {
        SnapshotSchedule schedule;
        for (;;) {
            NativeSnapshot value;
            if (!snapshot(value, fs)) {
                status.dropped.fetch_add(1, std::memory_order_relaxed);
            } else {
                const auto stamp = important(value);
                if (schedule.due(value[S::Tick], stamp)) {
                    status.state.store(NativeWorkerState::Saving, std::memory_order_release);
                    if (!fs.beginTransaction(svcGetSystemTick() + kOperationTicks)) {
                        result = kDeadline; break;
                    }
                    result = writer.save(fs, io, value);
                    if (result != 0) { break; }
                    status.verifiedSequence.store(writer.outcome().verifiedSequence,
                                                  std::memory_order_release);
                    schedule.saved(value[S::Tick], stamp);
                    status.state.store(NativeWorkerState::Running, std::memory_order_release);
                }
            }
            svcSleepThread(100000000);
        }
    }
    const auto closed = fs.closeAll();
    if (result == 0) { result = closed; }
    exitWorker(result, fs.quarantined());
}
} // 無名名前空間

NativeLoggerStatus& nativeLoggerStatus() { return status; }

void notifyNativeLoggerAfterSwap(std::uint32_t result) {
    if (operation.expire(svcGetSystemTick())) {
        status.lastError.store(kDeadline, std::memory_order_release);
        status.state.store(NativeWorkerState::Quarantined, std::memory_order_release);
        recordWriterTerminal(2);
    }
    if (!gate.afterSwap(result)) { return; }
    // Originalの成功後に一度だけ作成する。FSとWorker完了をこのthreadでは待たない。
    status.state.store(NativeWorkerState::Creating, std::memory_order_release);
    Handle handle = 0;
    const auto created = svcCreateThread(&handle, reinterpret_cast<void*>(worker), nullptr,
                                         stack + sizeof(stack), 59, -2);
    status.create.store(created, std::memory_order_release);
    recordProgress(Stage::WorkerCreate, created);
    if (created != 0 || handle == 0) {
        status.lastError.store(created != 0 ? created : kInvalidResponse, std::memory_order_release);
        status.state.store(NativeWorkerState::Stopped, std::memory_order_release);
        recordWriterTerminal(3);
        return;
    }
    status.state.store(NativeWorkerState::Starting, std::memory_order_release);
    const auto started = svcStartThread(handle);
    status.start.store(started, std::memory_order_release);
    recordProgress(Stage::WorkerStart, started);
    const auto closed = svcCloseHandle(handle);
    status.handleClose.store(closed, std::memory_order_release);
    recordProgress(Stage::WorkerHandleClose, closed);
    if (started != 0) {
        status.lastError.store(started, std::memory_order_release);
        status.state.store(NativeWorkerState::Stopped, std::memory_order_release);
        recordWriterTerminal(3);
    } else if (closed != 0) {
        status.lastError.store(closed, std::memory_order_release);
    }
    launchComplete.store(true, std::memory_order_release);
}
} // 名前空間 yw1::logging
