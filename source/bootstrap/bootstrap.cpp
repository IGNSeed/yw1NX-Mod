#include <common.hpp>
#include <lib/util/sys/cur_proc_handle.hpp>

#include <atomic>

#if YW1_BOOTSTRAP_SD_LOGGING || YW1_BOOTSTRAP_MEMORY_LOG
#include "logging/progress.hpp"
#endif
#if YW1_BOOTSTRAP_SD_LOGGING
#include "logging/sd_writer.hpp"
#endif
#if YW1_BOOTSTRAP_EXLAUNCH_DIAGNOSTIC || YW1_BOOTSTRAP_EGL_DIAGNOSTIC
#include "bootstrap/exlaunch_preflight.hpp"
#include "diagnostics/log_export.hpp"
#if YW1_BOOTSTRAP_EGL_CALLBACK
#include "diagnostics/callback_wire.hpp"
#if YW1_EGL_CONTEXT_PROBE
#include "bootstrap/context_diagnostic.hpp"
#endif
#endif
extern "C" char __module_start;
#endif
#if YW1_BOOTSTRAP_EGL_DIAGNOSTIC
namespace yw1::bootstrap {
void runEglDiagnostic();
}
#endif

#if YW1_BOOTSTRAP_FS_DIAGNOSTIC
namespace yw1::bootstrap {
void runFsDiagnostic();
}
#endif

extern "C" void exl_module_init();
extern "C" Result __real_svcMapProcessMemory(void *, Handle, u64, u64);
extern "C" Result __real_svcUnmapProcessMemory(void *, Handle, u64, u64);

namespace {

constexpr u64 kTitleId = 0x0100C0000CEEA000;
std::atomic<bool> initEntered{false};
std::atomic<bool> mainEntered{false};

template <std::size_t Size> void writeLog(const char (&message)[Size]) {
#if YW1_BOOTSTRAP_SD_LOGGING || YW1_BOOTSTRAP_MEMORY_LOG
    static_cast<void>(yw1::logging::memoryLog().append(message, Size - 1));
#endif
    // 出力の成功は実機ログを取得するまで認定しない。
    static_cast<void>(svcOutputDebugString(message, Size - 1));
}

void writeResult(const char *label, std::size_t length, Result result) {
#if YW1_BOOTSTRAP_SD_LOGGING || YW1_BOOTSTRAP_MEMORY_LOG
    static_cast<void>(yw1::logging::memoryLog().appendResult(label, length, result));
#endif
    static_cast<void>(svcOutputDebugString(label, length));
    char text[] = "0x00000000\n";
    constexpr char kDigits[] = "0123456789ABCDEF";
    for (unsigned int index = 0; index < 8; ++index) {
        text[2 + index] = kDigits[(result >> ((7 - index) * 4)) & 15];
    }
    static_cast<void>(svcOutputDebugString(text, sizeof(text) - 1));
}

} // 無名名前空間
extern "C" __attribute__((visibility("default"))) void yw1_bootstrap_init() {
    if (initEntered.exchange(true)) {
        return;
    }
#if YW1_BOOTSTRAP_EXTERNAL_DIAGNOSTICS
    yw1::logging::recordProgress(yw1::logging::Stage::InitEnter, 0);
#if YW1_BOOTSTRAP_EGL_CALLBACK
    yw1::diagnostics::initializeCallbackWire(
        static_cast<yw1::diagnostics::CallbackProfile>(YW1_CALLBACK_PROFILE));
#if YW1_EGL_CONTEXT_PROBE
    yw1::bootstrap::initializeContextDiagnostic();
#endif
#endif
#endif
#if YW1_BOOTSTRAP_EXLAUNCH_DIAGNOSTIC || YW1_BOOTSTRAP_EGL_DIAGNOSTIC
    using namespace yw1::logging;
    auto &state = yw1::diagnostics::yw1_module_diagnostics_state;
    yw1::diagnostics::setExportBootToken(svcGetSystemTick());
    state.writerPolicy.store(2, std::memory_order_release);
    // Writerをリンクしないprofileなので、exlaunch完了とは独立に外部回収を許可する。
    recordWriterTerminal(3);
#if YW1_BOOTSTRAP_EGL_DIAGNOSTIC
#if YW1_BOOTSTRAP_SAFE_HOOK_DIAGNOSTIC
    writeLog("\n=== YW1 B0.2.1 boot / safe hook prerequisites ===\nsource=module-bootstrap\n");
#else
    writeLog("\n=== YW1 B0.2 boot / EGL prerequisite diagnostics ===\nsource=module-bootstrap\n");
#endif
#else
    writeLog("\n=== YW1 B0.1.7 boot / exlaunch verification ===\nsource=module-bootstrap\n");
#endif
    writeLog("[YW1] dt_init_enter\n[YW1] direct_fs_writer=disabled\n");
    // すべての拒否経路でWriter未使用の終端を公開し、外部回収を可能にする。
    const auto finish = [] {
        writeLog("[YW1] bootstrap_init_returned\n");
        recordProgress(Stage::InitReturn, 0);
        recordWriterTerminal(3);
    };
    u64 titleId = 0;
    auto result = recordProgress(Stage::ProgramId,
                                 svcGetInfo(&titleId, InfoType_ProgramId, CUR_PROCESS_HANDLE, 0));
    recordObservedTitleId(titleId);
    constexpr char kProgram[] = "[YW1] program_id_result=";
    writeResult(kProgram, sizeof(kProgram) - 1, result);
    if (result == 0 && titleId != kTitleId) {
        recordProgress(Stage::GuardDenied, kWrongEntry);
        finish();
        return;
    }
    const auto handle = exl::util::proc_handle::Get();
    constexpr char kHandle[] = "[YW1] process_handle_result=";
    writeResult(kHandle, sizeof(kHandle) - 1, progress.processHandleResult.load());
    if (handle != INVALID_HANDLE) {
        titleId = 0;
        result =
            recordProgress(Stage::ProgramId, svcGetInfo(&titleId, InfoType_ProgramId, handle, 0));
        recordObservedTitleId(titleId);
        writeResult(kProgram, sizeof(kProgram) - 1, result);
    }
    if (handle == INVALID_HANDLE || result != 0 || titleId != kTitleId) {
        recordProgress(Stage::GuardDenied, result != 0 ? result : kInvalidResponse);
        finish();
        return;
    }
    result = recordProgress(Stage::ExlaunchGuard,
                            yw1::bootstrap::checkExlaunchEnvironment(&__module_start));
    constexpr char kGuard[] = "[YW1] exlaunch_guard_result=";
    writeResult(kGuard, sizeof(kGuard) - 1, result);
    if (result != 0) {
        recordProgress(Stage::GuardDenied, result);
        finish();
        return;
    }
    writeLog("[YW1] exlaunch_init_begin\n");
    recordProgress(Stage::Exlaunch, kInProgress);
    exl_module_init();
    recordProgress(Stage::Exlaunch, 0);
    writeLog("[YW1] exlaunch_init_returned\n");
    finish();
#elif YW1_BOOTSTRAP_FS_DIAGNOSTIC
    yw1::bootstrap::runFsDiagnostic();
#else
#if YW1_BOOTSTRAP_SD_LOGGING
    yw1::logging::recordProgress(yw1::logging::Stage::InitEnter, 0);
    writeLog("\n=== YW1 B0.1.3 boot / independent FS IPC ===\n");
#endif
    writeLog("[YW1 B0.1] init_enter; EGL hooks disabled\n");
    const Handle handle = exl::util::proc_handle::Get();
#if YW1_BOOTSTRAP_SD_LOGGING
    yw1::logging::recordProgress(yw1::logging::Stage::ProcessHandle,
                                 handle == INVALID_HANDLE ? yw1::logging::kInvalidResponse : 0);
#endif
    if (handle == INVALID_HANDLE) {
        writeLog("[YW1 B0.1] process_handle_unavailable; initialization skipped\n");
        return;
    }
    u64 titleId = 0;
    const Result result = svcGetInfo(&titleId, InfoType_ProgramId, handle, 0);
#if YW1_BOOTSTRAP_SD_LOGGING
    yw1::logging::recordProgress(yw1::logging::Stage::ProgramId, result);
    yw1::logging::recordObservedTitleId(titleId);
#endif
    if (R_FAILED(result) || titleId != kTitleId) {
        writeLog("[YW1 B0.1] program_id_mismatch_or_query_failed; initialization skipped\n");
        return;
    }
    writeLog("[YW1 B0.1] process_handle_ready; entering pinned exlaunch initialization\n");
    // ここから先の上流RwPages等の失敗はabortになり得るため、実機判定は保留する。
#if YW1_BOOTSTRAP_SD_LOGGING
    yw1::logging::recordProgress(yw1::logging::Stage::Exlaunch, yw1::logging::kInProgress);
#endif
    exl_module_init();
    writeLog("[YW1 B0.1] init_return; original game startup may continue\n");
#if YW1_BOOTSTRAP_SD_LOGGING
    yw1::logging::recordProgress(yw1::logging::Stage::InitReturn, 0);
    // ファイル操作は専用Workerだけで実行し、このcallbackを待たせない。
    yw1::logging::startSdLogger();
#endif
#endif
}

extern "C" void exl_main(void *, void *) {
    if (!mainEntered.exchange(true)) {
#if YW1_BOOTSTRAP_SD_LOGGING || YW1_BOOTSTRAP_MEMORY_LOG
        yw1::logging::recordProgress(yw1::logging::Stage::ExlMain, 0);
#endif
#if YW1_BOOTSTRAP_EXLAUNCH_DIAGNOSTIC || YW1_BOOTSTRAP_EGL_DIAGNOSTIC
        writeLog("[YW1] exl_main_reached\n");
#if YW1_BOOTSTRAP_EGL_DIAGNOSTIC
        yw1::bootstrap::runEglDiagnostic();
#endif
#else
        writeLog("[YW1 B0.1] exl_main_reached; no hooks, rendering, or input changes\n");
#endif
    }
}

extern "C" Result __wrap_svcMapProcessMemory(void *destination, Handle process, u64 source,
                                             u64 size) {
    // Module内のリンク時ラップだけで、ゲームのSVCや命令を書き換えない。
    writeLog("[YW1 B0.1] RwPages map begin\n");
#if YW1_BOOTSTRAP_SD_LOGGING || YW1_BOOTSTRAP_MEMORY_LOG
    yw1::logging::recordProgress(yw1::logging::Stage::RwMap, yw1::logging::kInProgress);
#endif
    const Result result = __real_svcMapProcessMemory(destination, process, source, size);
#if YW1_BOOTSTRAP_SD_LOGGING || YW1_BOOTSTRAP_MEMORY_LOG
    yw1::logging::recordProgress(yw1::logging::Stage::RwMap, result);
#endif
    constexpr char kLabel[] = "[YW1 B0.1] RwPages map result=";
    writeResult(kLabel, sizeof(kLabel) - 1, result);
    return result;
}

extern "C" Result __wrap_svcUnmapProcessMemory(void *destination, Handle process, u64 source,
                                               u64 size) {
    const Result result = __real_svcUnmapProcessMemory(destination, process, source, size);
#if YW1_BOOTSTRAP_SD_LOGGING || YW1_BOOTSTRAP_MEMORY_LOG
    yw1::logging::recordProgress(yw1::logging::Stage::RwUnmap, result);
#endif
    constexpr char kLabel[] = "[YW1 B0.1] RwPages unmap result=";
    writeResult(kLabel, sizeof(kLabel) - 1, result);
    return result;
}

extern "C" [[noreturn]] void exl_exception_entry() {
    // ModuleのDT_INIT経路では使用しない。誤ったentrypoint起動を継続しない。
    writeLog("[YW1 B0.1] unexpected_process_entry; stopped\n");
    for (;;) {
        svcSleepThread(1000000000);
    }
}
