#include "report.hpp"

#include <switch.h>

namespace {
constinit Result smResult = yw1::companion::kNotAttempted;
constinit Result appletResult = yw1::companion::kNotAttempted;
constinit Result hidResult = yw1::companion::kNotAttempted;
constinit Result fsResult = yw1::companion::kNotAttempted;
constinit yw1::companion::Report report;
constinit yw1::companion::BootstrapLog bootstrapLog;

bool consoleReady(const PrintConsole *screen) {
    return screen != nullptr && screen->consoleInitialised && screen->renderer != nullptr &&
           screen->renderer->drawChar != nullptr && screen->renderer->scrollWindow != nullptr &&
           screen->renderer->flushAndSwap != nullptr && screen->renderer->deinit != nullptr;
}

// 標準__appInitのWindow準備を、abortする内部entryを使わず実Resultで実行する。
class WindowSession {
  public:
    Result initialize(yw1::companion::Report &log) {
        auto rc = log.result("vi_initialize_result", viInitialize(ViServiceType_Default));
        if (rc != 0) {
            return rc;
        }
        viReady_ = true;
        rc = log.result("vi_open_display_result", viOpenDefaultDisplay(&display_));
        if (rc != 0) {
            return rc;
        }
        displayReady_ = true;
        rc = log.result("vi_create_layer_result", viCreateLayer(&display_, &layer_));
        if (rc != 0) {
            return rc;
        }
        layerReady_ = true;
        rc = log.result("vi_scaling_result",
                        viSetLayerScalingMode(&layer_, ViScalingMode_FitToLayer));
        if (rc != 0) {
            return rc;
        }
        window_ = nwindowGetDefault();
        if (window_ == nullptr || nwindowIsValid(window_)) {
            return log.result("default_window_ownership_check", yw1::companion::kInvalidData);
        }
        rc = log.result("nwindow_create_result", nwindowCreateFromLayer(window_, &layer_));
        if (rc != 0) {
            return rc;
        }
        windowReady_ = true;
        return log.result("nwindow_dimensions_result", nwindowSetDimensions(window_, 1280, 720));
    }
    void close(yw1::companion::Report &log) {
        if (windowReady_) {
            nwindowClose(window_);
            windowReady_ = false;
            log.append("nwindow_close_issued=true\n");
        }
        if (layerReady_) {
            log.result("vi_close_layer_result", viCloseLayer(&layer_));
            layerReady_ = false;
        }
        if (displayReady_) {
            log.result("vi_close_display_result", viCloseDisplay(&display_));
            displayReady_ = false;
        }
        if (viReady_) {
            viExit();
            viReady_ = false;
            log.append("vi_exit_issued=true\n");
        }
    }

  private:
    ViDisplay display_{};
    ViLayer layer_{};
    NWindow *window_ = nullptr;
    bool viReady_ = false;
    bool displayReady_ = false;
    bool layerReady_ = false;
    bool windowReady_ = false;
};

// 保存失敗時も、全Resultを画面から回収できるように固定幅でページ分割する。
std::size_t showDetails(PrintConsole *screen, std::size_t page) {
    if (!consoleReady(screen)) {
        return 0;
    }
    consoleClear();
    std::printf("Details page %zu   L/R: page, A: summary, B: back\n\n", page + 1);
    std::size_t row = 0;
    std::size_t column = 0;
    for (std::size_t i = 0; i < report.size; ++i) {
        const auto byte = report.text[i];
        if (row / 30 == page) {
            std::putchar(byte);
        }
        if (byte == '\n') {
            ++row;
            column = 0;
        } else if (++column == 76) {
            if (row / 30 == page) {
                std::putchar('\n');
            }
            ++row;
            column = 0;
        }
    }
    return row / 30;
}
} // 無名名前空間
extern "C" Result __real_framebufferCreate(Framebuffer *, NWindow *, u32, u32, u32, u32);
extern "C" Result __real_framebufferMakeLinear(Framebuffer *);

// NRO自身の呼出しだけを包み、SDKの実Resultをそのまま記録・返却する。
extern "C" Result __wrap_framebufferCreate(Framebuffer *fb, NWindow *window, u32 width, u32 height,
                                           u32 format, u32 count) {
    return report.result("framebuffer_create_result",
                         __real_framebufferCreate(fb, window, width, height, format, count));
}
extern "C" Result __wrap_framebufferMakeLinear(Framebuffer *fb) {
    return report.result("framebuffer_linear_result", __real_framebufferMakeLinear(fb));
}

// 通常のlibnx crt0/TLS/heapを使用するが、FS初期化失敗をmain前のabortにしない。
extern "C" void __appInit() {
    smResult = smInitialize();
    if (smResult != 0) {
        return;
    }
    if (hosversionGet() == 0 && setsysInitialize() == 0) {
        SetSysFirmwareVersion version{};
        if (setsysGetFirmwareVersion(&version) == 0) {
            hosversionSet(MAKEHOSVERSION(version.major, version.minor, version.micro));
        }
        setsysExit();
    }
    appletResult = appletInitialize();
    if (appletResult == 0) {
        hidResult = hidInitialize();
    }
    fsResult = fsInitialize();
}

extern "C" void __appExit() {
    if (fsResult == 0) {
        fsExit();
    }
    if (hidResult == 0) {
        hidExit();
    }
    if (appletResult == 0) {
        appletExit();
    }
    if (smResult == 0) {
        smExit();
    }
}

int main() {
    using namespace yw1::companion;
    report.append("\n=== YW1 Diagnostics %s ===\nsource=companion-nro\n"
                  "title_id=0100C0000CEEA000\n",
                  yw1::diagnostics::kDiagnosticVersion);
    report.result("sm_startup_result", smResult);
    report.result("applet_startup_result", appletResult);
    report.result("hid_startup_result", hidResult);
    report.result("fs_startup_result", fsResult);
    const bool album = appletResult == 0 && appletGetAppletType() == AppletType_LibraryApplet;
    report.append("album_library_applet=%s\n", album ? "true" : "false");
    Observation observation;
    if (album && smResult == 0) {
        observation = inspectTarget(report, &bootstrapLog);
    } else {
        report.append("inspection_not_attempted=requires_album_library_applet\n");
    }
    report.append("module_detected=%d\ndt_init_reached=%d\ninspection_result=0x%08X\n",
                  observation.moduleDetected, observation.dtInitReached, observation.result);
    bool writerActive = observation.moduleDetected == 1;
    if (observation.snapshotRead) {
        const auto &state = observation.snapshot;
        writerActive = state.writerTerminal != 1 && state.writerTerminal != 3;
    }
    FileOutcome file;
    std::size_t savedThrough = 0;
    // UIの初期化前に実診断結果を保存する。以後のcheckpointも固定回数だけ追記する。
    const auto persist = [&](const char *phase) {
        report.append("nro_phase=%s first_anomaly_result=0x%08X\n", phase, report.firstAnomaly);
        if (fsResult == 0 && !writerActive) {
            file = saveReport(report, savedThrough);
            if (file.savedThrough != 0) {
                savedThrough = file.savedThrough;
            }
        } else {
            report.append("sd_save_not_attempted=%s\n",
                          writerActive ? "game_writer_active" : "fs_unavailable");
        }
    };
    persist("bootstrap_export_begin");
    std::uint32_t exportSaveResult = kNotAttempted;
    if (bootstrapLog.readResult == 0 && fsResult == 0 && !writerActive) {
        exportSaveResult = saveBootstrapLog(report, bootstrapLog);
    } else {
        report.append("bootstrap_export_not_saved read_result=0x%08X writer_active=%s\n",
                      bootstrapLog.readResult, writerActive ? "true" : "false");
    }
    persist("ui_init_begin");
    WindowSession window;
    PrintConsole *screen = nullptr;
    bool consoleAttempted = false;
    if (appletResult == 0) {
        if (window.initialize(report) == 0) {
            consoleAttempted = true;
            screen = consoleInit(nullptr);
        }
    }
    const bool initialized = consoleReady(screen);
    report.append("console_init_attempted=%s console_nonnull=%s console_initialised=%s "
                  "renderer_ready=%s\n",
                  consoleAttempted ? "true" : "false", screen != nullptr ? "true" : "false",
                  screen != nullptr && screen->consoleInitialised ? "true" : "false",
                  initialized ? "true" : "false");
    if (appletResult == 0 && !initialized) {
        report.result("console_readiness_check", kUnavailable);
    }
    persist(initialized ? "ui_init_returned_ready" : "ui_init_failed_no_draw");
    if (initialized) {
        const auto summary = [&]() {
            if (!consoleReady(screen)) {
                return;
            }
            consoleClear();
            std::printf("YW1 Diagnostics %s\nsource=companion-nro\n\n",
                        yw1::diagnostics::kDiagnosticVersion);
            const auto init = initialization(observation);
            std::printf("exlaunch_init_reached=%d exl_main_reached=%d\n"
                        "exlaunch_init_returned=%d direct_fs_writer_enabled=%d\n",
                        init.exlaunchReached, init.mainReached, init.exlaunchReturned,
                        init.directWriterEnabled);
            std::printf("Bootstrap read: %08X save: %08X duplicate skipped: %s\n",
                        bootstrapLog.readResult, exportSaveResult,
                        bootstrapLog.duplicate ? "yes" : "no");
            std::printf("Album mode: %s\nPID: %llu\nModules: %d\nModule: %d  DT_INIT: %d\n",
                        album ? "yes" : "NO - use Album",
                        static_cast<unsigned long long>(observation.pid), observation.moduleCount,
                        observation.moduleDetected, observation.dtInitReached);
            std::printf(
                "Inspection: 0x%08X\nSD open: 0x%08X\nDirectory: 0x%08X\nFile open: 0x%08X\n",
                observation.result, file.sdOpen, file.directory, file.fileOpen);
            std::printf("Write: 0x%08X  Flush: 0x%08X\nReceipt write: 0x%08X  flush: 0x%08X\n",
                        file.write, file.flush, file.receiptWrite, file.receiptFlush);
            std::printf("File size/limit preparation: 0x%08X\n", file.prepare);
            std::printf("Close issued: %s (void API)\n", file.closeIssued ? "yes" : "no");
            std::printf("SM/AM/HID/FS: %08X / %08X / %08X / %08X\n", smResult, appletResult,
                        hidResult, fsResult);
            if (observation.snapshotRead) {
                std::printf("Latest stage: %llu  result: %08X\n",
                            static_cast<unsigned long long>(observation.snapshot.latest >> 32),
                            static_cast<unsigned>(observation.snapshot.latest));
            }
            if (writerActive) {
                std::printf("Game writer active: return to game, then retry.\n");
            }
            if (observation.cleanupFailed) {
                std::printf("Alias cleanup failed: CLOSE Album before retry.\n");
            }
            std::printf("-1=unknown, 0=no, 1=yes; FFFFFFFF=not attempted\n\nA: details (all "
                        "Results), B: back\n");
        };
        persist("ui_draw_begin");
        summary();
        bool updateRecorded = false;
        bool details = false;
#if YW1_BOOTSTRAP_EGL_DIAGNOSTIC
        bool eglPage = false;
        const auto showEgl = [&] {
            if (!consoleReady(screen)) {
                return;
            }
            consoleClear();
            std::printf("EGL / OpenGL - game process observation\nNRO VI/Framebuffer is separate\n"
                        "Read result: %08X\n",
                        observation.eglReadResult);
            if (observation.eglReadResult != 0) {
                std::printf("Game EGL values: unknown\n");
                return;
            }
            const auto &egl = observation.egl;
            std::printf("Hook attempted: %u result: %08X\nGuard: %u blocked reason: %u\n"
                        "Callback: %llu Original calls/returns: %llu / %llu\n",
                        egl.installAttempted, egl.installResult, egl.guardStatus, egl.blockedReason,
                        static_cast<unsigned long long>(egl.callbackCount),
                        static_cast<unsigned long long>(egl.originalCalls),
                        static_cast<unsigned long long>(egl.originalReturns));
            if (egl.bindingKnown) {
                std::printf("API: %04X Context: %s Thread: %llu\n", egl.api,
                            egl.context ? "present" : "none",
                            static_cast<unsigned long long>(egl.thread));
            } else {
                std::printf("Current API / Context / Render Thread: unknown\n");
            }
            if (egl.queryFlags & yw1::diagnostics::kStringsIssued) {
                std::printf("GL: %.80s\nVendor: %.80s\nRenderer: %.80s\n", egl.version, egl.vendor,
                            egl.renderer);
            } else {
                std::printf("GL / Vendor / Renderer: unknown\n");
            }
            if (egl.queryFlags & yw1::diagnostics::kGlslIssued) {
                std::printf("GLSL: %.80s\n", egl.glsl);
            } else {
                std::printf("GLSL: unknown\n");
            }
            if (egl.queryFlags & yw1::diagnostics::kViewportIssued) {
                std::printf("Viewport: %d,%d,%d,%d\n", egl.viewport[0], egl.viewport[1],
                            egl.viewport[2], egl.viewport[3]);
            } else {
                std::printf("Viewport: unknown\n");
            }
            if (egl.queryFlags & yw1::diagnostics::kFramebufferIssued) {
                std::printf("Framebuffer: %d\n", egl.framebuffer);
            } else {
                std::printf("Framebuffer: unknown\n");
            }
            if (egl.queryFlags & yw1::diagnostics::kProgramIssued) {
                std::printf("Program: %d\n", egl.program);
            } else {
                std::printf("Program: unknown\n");
            }
            std::printf("Snapshot status (MOD): %08X\nHook is disabled in this build\nA: summary "
                        "L: details B: back\n",
                        egl.snapshotStatus);
        };
#endif
        std::size_t page = 0;
        std::size_t lastPage = 0;
        PadState pad{};
        if (hidResult == 0) {
            padConfigureInput(1, HidNpadStyleSet_NpadStandard);
            padInitializeDefault(&pad);
        }
        while (appletMainLoop()) {
            if (!consoleReady(screen)) {
                report.result("console_readiness_lost", kUnavailable);
                break;
            }
            if (hidResult == 0) {
                padUpdate(&pad);
                const auto down = padGetButtonsDown(&pad);
                if (down & HidNpadButton_B) {
                    break;
                }
                if (down & HidNpadButton_A) {
#if YW1_BOOTSTRAP_EGL_DIAGNOSTIC
                    if (eglPage) {
                        details = true;
                    }
                    eglPage = false;
#endif
                    details = !details;
                    page = 0;
                    if (details) {
                        lastPage = showDetails(screen, page);
                    } else {
                        summary();
                    }
                }
#if YW1_BOOTSTRAP_EGL_DIAGNOSTIC
                else if (eglPage && (down & HidNpadButton_L)) {
                    eglPage = false;
                    details = true;
                    lastPage = showDetails(screen, page);
                } else if (details && !eglPage && (down & HidNpadButton_R) && page == lastPage) {
                    eglPage = true;
                    showEgl();
                }
#endif
                else if (details && (down & HidNpadButton_L) && page != 0) {
                    lastPage = showDetails(screen, --page);
                } else if (details && (down & HidNpadButton_R) && page < lastPage) {
                    lastPage = showDetails(screen, ++page);
                }
            }
            if (!consoleReady(screen)) {
                break;
            }
            consoleUpdate(screen);
            if (!updateRecorded) {
                persist("ui_first_update_returned");
                updateRecorded = true;
            }
            svcSleepThread(16000000);
        }
        report.append("nro_phase=ui_exit_begin\n");
        if (consoleReady(screen)) {
            consoleExit(screen);
            report.append("console_exit_returned=true\n");
        } else {
            report.append("console_exit_skipped=renderer_not_ready\n");
        }
    }
    window.close(report);
    report.append("nro_phase=ui_cleanup_complete\nnro_main_return=0\n");
    persist("main_cleanup_complete");
    return 0;
}
