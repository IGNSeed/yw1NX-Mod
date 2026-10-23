#include "progress.hpp"
#include "sd_writer.hpp"

#include <common.hpp>

namespace yw1::logging {
namespace {

constinit IpcStorage storage;
constinit SdWriter writer;
constinit std::atomic<bool> workerStarted{false};
constinit std::atomic<bool> launchComplete{false};
constinit SaveOutcome lastOutcome;
// スタックとIPC参照領域はモジュールがプロセス終了まで所有する。
alignas(4096) unsigned char workerStack[64 * 1024];

Result connect(void*, Handle* handle, const char* name) {
    return svcConnectToNamedPort(handle, name);
}
Result submit(void*, Handle* event, void* data, std::size_t size, Handle session) {
    return svcSendAsyncRequestWithUserBuffer(event, data, size, session);
}
Result wait(void*, Handle handle, std::uint64_t timeout) {
    s32 index = -1;
    const auto result = svcWaitSynchronization(&index, &handle, 1, timeout);
    return result == 0 && index != 0 ? kInvalidResponse : result;
}
Result close(void*, Handle handle) {
    return svcCloseHandle(handle);
}
std::uint64_t ticks(void*) {
    return svcGetSystemTick();
}

[[noreturn]] void worker(void*) {
    recordProgress(Stage::WorkerEnter, 0);
    // raw kernel threadではNNSDK ThreadType/newlib/libnx TLSを初期化も参照もしない。
    // 使用関数は自作コードとraw SVCだけ。起動元の記録完了前には保存を始めない。
#if !YW1_BOOTSTRAP_FS_DIAGNOSTIC
    svcSleepThread(50000000);
#endif
    const auto readyDeadline = svcGetSystemTick() + 1920000;
    while (!launchComplete.load(std::memory_order_acquire)) {
        if (svcGetSystemTick() >= readyDeadline) {
            recordProgress(Stage::WorkerEnter, kDeadline);
            svcExitThread();
            __builtin_unreachable();
        }
        svcSleepThread(1000000);
    }
    constexpr char kStart[] = "[YW1] worker_enter\n";
    static_cast<void>(memoryLog().append(kStart, sizeof(kStart) - 1));
    const KernelApi api{nullptr, connect, submit, wait, close, ticks};
    IpcFs fs(api, storage, svcGetSystemTick() + 8 * 19200000);
    lastOutcome = writer.save(memoryLog(), fs, storage);
    recordProgress(Stage::Saved, lastOutcome.result);
    char line[128];
    constexpr char kLabel[] = "[YW1] logger_final_result=";
    const auto size =
        formatResult(line, sizeof(line), kLabel, sizeof(kLabel) - 1, lastOutcome.result);
    static_cast<void>(svcOutputDebugString(line, size));
    // timeout時もIPCのpath/packet/送信データは静的領域に残す。stackは送信しない。
    svcExitThread();
    __builtin_unreachable();
}

} // 無名名前空間

void startSdLogger(int priority) {
    if (workerStarted.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    // 起動ThreadのTLS、SM、FS、allocatorを一切借用しない。失敗はResultで戻す。
    Handle handle = 0;
    recordProgress(Stage::WorkerCreate, kInProgress);
    auto result = svcCreateThread(&handle, reinterpret_cast<void*>(worker), nullptr,
                                  workerStack + sizeof(workerStack), priority, -2);
    progress.workerCreate.store(result, std::memory_order_release);
    recordProgress(Stage::WorkerCreate, result);
    constexpr char kCreate[] = "[YW1] worker_create_result=";
    static_cast<void>(memoryLog().appendResult(kCreate, sizeof(kCreate) - 1, result));
    if (result != 0 || handle == 0) {
        return;
    }
    recordProgress(Stage::WorkerStart, kInProgress);
    result = svcStartThread(handle);
    progress.workerStart.store(result, std::memory_order_release);
    recordProgress(Stage::WorkerStart, result);
    constexpr char kStart[] = "[YW1] worker_start_result=";
    static_cast<void>(memoryLog().appendResult(kStart, sizeof(kStart) - 1, result));
    // Workerはこの起動元がResult記録とhandle解放を完了するまで保存しない。
    // 成功時にはkernel自身が実行中threadの参照を保持する。
    const auto closeResult = svcCloseHandle(handle);
    progress.workerHandleClose.store(closeResult, std::memory_order_release);
    recordProgress(Stage::WorkerHandleClose, closeResult);
    constexpr char kClose[] = "[YW1] worker_handle_close_result=";
    static_cast<void>(memoryLog().appendResult(kClose, sizeof(kClose) - 1, closeResult));
    if (closeResult != 0) {
        char line[128];
        const auto size = formatResult(line, sizeof(line), kClose, sizeof(kClose) - 1, closeResult);
        static_cast<void>(svcOutputDebugString(line, size));
    }
    launchComplete.store(true, std::memory_order_release);
}

} // 名前空間 yw1::logging
