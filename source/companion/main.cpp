#include "report.hpp"
#if YW1_EGL_CONTEXT_PROBE
#include "context_report.hpp"
#endif

#include <switch.h>

namespace {
constinit Result smResult = yw1::companion::kNotAttempted;
constinit Result appletResult = yw1::companion::kNotAttempted;
constinit Result hidResult = yw1::companion::kNotAttempted;
constinit Result fsResult = yw1::companion::kNotAttempted;
constinit yw1::companion::Report report;
constinit yw1::companion::BootstrapLog bootstrapLog;

bool consoleReady(const PrintConsole* screen) {
    return screen != nullptr && screen->consoleInitialised && screen->renderer != nullptr &&
           screen->renderer->drawChar != nullptr && screen->renderer->scrollWindow != nullptr &&
           screen->renderer->flushAndSwap != nullptr && screen->renderer->deinit != nullptr;
}

// 標準__appInitのWindow準備を、abortする内部entryを使わず実Resultで実行する。
class WindowSession {
  public:
    Result initialize(yw1::companion::Report& log) {
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
    void close(yw1::companion::Report& log) {
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
    NWindow* window_ = nullptr;
    bool viReady_ = false;
    bool displayReady_ = false;
    bool layerReady_ = false;
    bool windowReady_ = false;
};

// 保存失敗時も、全Resultを画面から回収できるように固定幅でページ分割する。
std::size_t showDetails(PrintConsole* screen, std::size_t page) {
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
extern "C" Result __real_framebufferCreate(Framebuffer*, NWindow*, u32, u32, u32, u32);
extern "C" Result __real_framebufferMakeLinear(Framebuffer*);

// NRO自身の呼出しだけを包み、SDKの実Resultをそのまま記録・返却する。
extern "C" Result __wrap_framebufferCreate(Framebuffer* fb, NWindow* window, u32 width, u32 height,
                                           u32 format, u32 count) {
    return report.result("framebuffer_create_result",
                         __real_framebufferCreate(fb, window, width, height, format, count));
}
extern "C" Result __wrap_framebufferMakeLinear(Framebuffer* fb) {
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
                  kCompanionVersion);
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
#if YW1_COMPANION_EGL_GOT_PROBE
    appendGotReport(report, observation, bootstrapLog);
#endif
#if YW1_COMPANION_EGL_CALLBACK
    appendCallbackReport(report, observation);
#endif
#if YW1_EGL_CONTEXT_PROBE
    appendContextReport(report, observation);
#endif
    bool writerActive = observation.moduleDetected == 1;
    if (observation.snapshotRead) {
        const auto& state = observation.snapshot;
        writerActive = state.writerTerminal != 1 && state.writerTerminal != 3;
    }
    FileOutcome file;
    std::size_t savedThrough = 0;
    // UIの初期化前に実診断結果を保存する。以後のcheckpointも固定回数だけ追記する。
    const auto persist = [&](const char* phase) {
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
    PrintConsole* screen = nullptr;
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
            std::printf("YW1 Diagnostics %s\nsource=companion-nro\n\n", kCompanionVersion);
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
#if YW1_COMPANION_EGL_GOT_PROBE
        bool gotPage = false;
#if YW1_COMPANION_EGL_TARGET_MEMORY
        bool targetMemoryPage = false;
#if YW1_COMPANION_EGL_CODE_SNAPSHOT
        bool codeSnapshotPage = false;
#if YW1_COMPANION_EGL_CODE_ATTRIBUTION
        unsigned codeSnapshotPart = 0;
#endif
#endif
#endif
        const auto showGot = [&] {
            if (!consoleReady(screen)) {
                return;
            }
            consoleClear();
            const auto& got = observation.got;
            const auto tri = [](int value) {
                return value < 0 ? "unknown" : (value ? "yes" : "no");
            };
            std::printf("EGL GOT Probe %s\nRead-only game process snapshot from Album\n"
                        "Main ID: %s PID: %llu Read Result: %08X\nSlot Offset: 0x8B4398\n",
                        kCompanionVersion, got.mainVerified ? "verified" : "not verified",
                        static_cast<unsigned long long>(observation.pid), got.result);
            if (got.valueKnown) {
                std::printf("Slot / Companion Value: %016llX\n",
                            static_cast<unsigned long long>(got.slotValue));
            } else {
                std::printf("Slot / Companion Value: unknown\n");
            }
            std::printf("Target Module: %s\n", gotTargetName(got.target));
            if (got.normalizedKnown) {
                std::printf("Target Offset: %llX\nNormalized Target: %s+%llX\n",
                            static_cast<unsigned long long>(got.targetOffset),
                            gotTargetName(got.target),
                            static_cast<unsigned long long>(got.targetOffset));
            } else {
                std::printf("Target Offset / Normalized Target: unknown\n");
            }
            std::printf("Expected Export: %s Executable page: %s\n", tri(got.matchesExpected),
                        tri(got.executable));
            if (got.pagePermission == UINT32_MAX) {
                std::printf("Game Page Permission: unknown\n");
            } else {
                std::printf("Game Page Permission: %X (game handle)\n", got.pagePermission);
            }
            std::printf("Binding State: %s\nBootstrap Value: not collected (module unchanged)\n"
                        "Export/GOT Hook Active: no GOT Modification: disabled\n"
                        "Compare full Build ID + offset between boots.\n"
                        "Album snapshot does not prove stability during play.\n"
                        "A: summary L: EGL/OpenGL B: back\n",
                        gotBindingName(got.binding));
#if YW1_COMPANION_EGL_TARGET_MEMORY
            std::printf("R: Target Memory\n");
#endif
        };
#if YW1_COMPANION_EGL_TARGET_MEMORY
        const auto showTargetMemory = [&] {
            if (!consoleReady(screen)) {
                return;
            }
            consoleClear();
            const auto& got = observation.got;
            const auto& memory = got.memory;
            std::printf("EGL GOT Target Memory %s\nSource: game process  Read-only: yes\n"
                        "GOT Read: %08X Target Query: %08X\nQuery attempted: %s\n",
                        kCompanionVersion, got.readResult, memory.queryResult,
                        targetBoolean(memory.queryAttempted));
            if (got.valueKnown) {
                const auto relative = relativeAddress(got.slotValue, got.mainBase);
                std::printf("Target: %016llX\nRelative to Main: %s0x%llX\n",
                            static_cast<unsigned long long>(got.slotValue),
                            relative.negative ? "-" : "+",
                            static_cast<unsigned long long>(relative.magnitude));
            } else {
                std::printf("Target / Relative to Main: unknown\n");
            }
            if (memory.infoReturned) {
                const auto& region = memory.region;
                const auto relative = relativeAddress(region.base, got.mainBase);
                std::printf("Region Base: %016llX Size: %llX\nRegion relative: %s0x%llX\n"
                            "Memory Type: %08X %s\nAttributes: %08X (known: %s)\n"
                            "Permission: %08X (known: %s)\nIPC ref: %u Device ref: %u\n",
                            static_cast<unsigned long long>(region.base),
                            static_cast<unsigned long long>(region.size),
                            relative.negative ? "-" : "+",
                            static_cast<unsigned long long>(relative.magnitude), region.type,
                            targetMemoryTypeName(region.type), region.attributes,
                            targetBoolean(memory.attributesKnown), region.permission,
                            targetBoolean(memory.permissionKnown), region.ipcRefcount,
                            region.deviceRefcount);
            } else {
                std::printf("Region Base / Size / Relative: unknown\nMemory Type: unknown\n"
                            "Attributes / Permission / Refcounts: unknown\n");
            }
            std::printf("Region Valid: %s Contains Pointer: %s\nMapped: %s\n"
                        "Readable: %s Writable: %s Executable: %s\nValidation: %08X\n",
                        targetBoolean(memory.infoReturned ? (memory.regionValid ? 1 : 0) : -1),
                        targetBoolean(memory.containsPointer), targetBoolean(memory.mapped),
                        targetBoolean(memory.readable), targetBoolean(memory.writable),
                        targetBoolean(memory.executable), memory.validationResult);
#if YW1_COMPANION_EGL_CODE_SNAPSHOT
            std::printf("Code Snapshot Result: %08X Valid: %s\nR: Code Snapshot\n",
                        got.snapshot.result, targetBoolean(got.snapshot.bytesValid));
#else
            std::printf("Code Read: not attempted FFFFFFFF\nCode bytes: unknown\n"
                        "Code snapshot disabled pending mapping/lifetime evidence.\n");
#endif
            std::printf("Executable page does not verify a function or owner.\n"
                        "Export/GOT Hook Active: no GOT Modification: disabled\n"
                        "Album snapshot does not prove stability during play.\n"
                        "A: summary L: GOT Probe B: back\n");
        };
#if YW1_COMPANION_EGL_CODE_SNAPSHOT
        const auto showCodeSnapshot = [&] {
            if (!consoleReady(screen)) {
                return;
            }
            consoleClear();
            const auto& got = observation.got;
            const auto& snapshot = got.snapshot;
#if YW1_EGL_CONTEXT_PROBE
            if (codeSnapshotPart >= 4) {
                showContextPage(observation, codeSnapshotPart == 5);
                return;
            }
#endif
#if YW1_COMPANION_EGL_CALLBACK
            if (codeSnapshotPart == 3) {
                using Field = yw1::diagnostics::CallbackField;
                const auto& wire = observation.callback;
                std::printf("EGL Callback B0.2.8\nSource: game module\n"
#if YW1_EGL_CONTEXT_PROBE
                            "Build Profile: %s  GL: read probe / ImGui: disabled\n"
#else
                            "Build Profile: %s  GL/ImGui: disabled\n"
#endif
                            "Read: %08X Legacy ABI: %s\n",
                            YW1_CALLBACK_PROFILE == 2 ? "B / Canary" : "A / Read-only",
                            observation.callbackReadResult, observation.legacyModule ? "yes" : "no");
                if (observation.callbackReadResult != 0) {
                    std::printf("Callback state / counts: unknown\n%s\n"
                                "A: summary L: Original EGL B: back\n",
                                observation.legacyModule ? "Legacy module: no callback publication."
                                    : "Unpublished, unstable or failed read; see Result.");
                    return;
                }
                const auto number = [&](const char* name, Field field) {
                    std::printf("%s: %llu\n", name,
                                static_cast<unsigned long long>(wire[field]));
                };
                number("Module Profile (1=A, 2=B)", Field::Profile);
                std::printf("Method: Main single BL-to-BL\nTarget: %016llX Original PLT: %016llX\n"
                            "Guard: %08X Install: %08X\n",
                            static_cast<unsigned long long>(wire[Field::Target]),
                            static_cast<unsigned long long>(wire[Field::Original]),
                            static_cast<unsigned>(wire[Field::GuardResult]),
                            static_cast<unsigned>(wire[Field::InstallResult]));
                number("Install Attempted", Field::InstallAttempted);
                number("Original Published", Field::OriginalPublished);
                number("Hook Active (2=uncertain)", Field::HookActive);
                number("Callback Count", Field::CallbackCount);
                number("Original Call Count", Field::OriginalCalls);
                number("Original Return Count", Field::OriginalReturns);
                if (wire[Field::OriginalReturns]) {
                    std::printf("Last Original Result: %08X\n",
                                static_cast<unsigned>(wire[Field::LastOriginalResult]));
                } else { std::printf("Last Original Result: unknown\n"); }
                number("Reentry / Overlap Count", Field::ReentryCount);
                std::printf("Thread ID: unknown\nHook Error: %08X\nRollback: %llu / %08X\n"
                            "Mapping: %08X Cache: %08X Cleanup: %08X\n",
                            static_cast<unsigned>(wire[Field::HookError]),
                            static_cast<unsigned long long>(wire[Field::RollbackAttempted]),
                            static_cast<unsigned>(wire[Field::RollbackResult]),
                            static_cast<unsigned>(wire[Field::MappingResult]),
                            static_cast<unsigned>(wire[Field::CacheResult]),
                            static_cast<unsigned>(wire[Field::CleanupResult]));
                number("Initialization Stage", Field::InitializationStage);
#if YW1_EGL_CONTEXT_PROBE
                std::printf("R: EGL / GL Context\n");
#endif
                std::printf("Status: %s\nReason: %s\nGame activity during NRO: unknown\n"
                            "GOT Modification: disabled\nA: summary L: Original EGL B: back\n",
                            yw1::diagnostics::callbackActivity(wire),
                            yw1::diagnostics::callbackReason(wire));
                return;
            }
#endif
#if YW1_COMPANION_SALTY_ORIGINAL_EGL
            if (codeSnapshotPart == 2) {
                const auto& original = got.original;
                std::printf("SaltyNX Original EGL B0.2.7\nSource: game process  Mode: read-only\n"
                            "SaltyNX Version: 1.9.3 reference\nCore Identity: %08X\n",
                            original.identity);
                if (original.coreKnown) {
                    std::printf("Core: %016llX GOT Target: %016llX\nEGL::Swap Offset: 0x1E80\n"
                                "Original Storage: %016llX (+0x2A3B8)\n",
                                static_cast<unsigned long long>(original.coreBase),
                                static_cast<unsigned long long>(got.slotValue),
                                static_cast<unsigned long long>(original.storageAddress));
                } else {
                    std::printf("Core / Original Storage: unknown\n");
                }
                if (original.storageInfoReturned) {
                    const auto& region = original.storage.region;
                    std::printf("Storage Region: %016llX +%llX\nStorage Type: %X Permission: %X\n",
                                static_cast<unsigned long long>(region.base),
                                static_cast<unsigned long long>(region.size), region.type,
                                region.permission);
                } else {
                    std::printf("Storage Region / Type / Permission: unknown\n");
                }
                std::printf("Result: %08X Preflight: %08X\nBefore: %08X / %08X\n"
                            "Map: %08X Read: %08X Unmap: %08X\n"
                            "After Storage: %08X Postvalidation: %08X\n",
                            original.result, original.preflight, original.beforeValidation,
                            original.beforeStorage, original.map, original.read, original.unmap,
                            original.afterStorage, original.postvalidation);
                if (original.snapshotValid) {
                    std::printf("Original Pointer: %016llX\n",
                                static_cast<unsigned long long>(original.pointer));
                } else {
                    std::printf("Original Pointer: unknown\n");
                }
                if (original.moduleKnown) {
                    std::printf("Original Module: %s Offset: +%llX\n", original.moduleName,
                                static_cast<unsigned long long>(original.moduleOffset));
                } else {
                    std::printf("Original Module / Relative Offset: unknown\n");
                }
                std::printf("Original Executable: %s\nClassification: %s\n"
                            "Matches subsdk0 export: %s\nSnapshot Valid: %s Quarantined: %s\n"
                            "Identity: 64-byte window/layout only\n"
                            "Export/GOT Hook Active: no GOT Modification: disabled\n"
                            "A: summary L: HEX page B: back\n",
                            targetBoolean(original.executable), originalPointerKindName(original.kind),
                            targetBoolean(original.expectedExport), targetBoolean(original.snapshotValid),
                            targetBoolean(original.readerQuarantined));
#if YW1_COMPANION_EGL_CALLBACK
                std::printf("R: EGL Callback\n");
#endif
                return;
            }
#endif
            std::printf("EGL GOT Code Snapshot %s\nSource: game process  Mode: read-only\n"
                        "GOT Read: %08X Target Query: %08X\n",
                        kCompanionVersion, got.readResult, got.memory.queryResult);
#if YW1_COMPANION_EGL_CODE_ATTRIBUTION
            if (codeSnapshotPart == 1) {
                std::printf("HEX page - fixed maximum %u bytes\nValid: %s Quarantined: %s\n",
                            kSnapshotLength, targetBoolean(snapshot.bytesValid),
                            targetBoolean(snapshot.cleanupQuarantined));
                if (snapshot.bytesValid) {
                    for (unsigned i = 0; i < kSnapshotLength; ++i) {
                        if (i % 16 == 0) { std::printf("+%02X: ", i); }
                        std::printf("%02X%s", snapshot.bytes[i],
                                    i % 16 == 15 ? "\n" : (i % 4 == 3 ? " " : ""));
                    }
                } else {
                    std::printf("Bytes: unknown (snapshot rejected)\n");
                }
                std::printf("No target execution or branch following.\n"
                            "Export/GOT Hook Active: no GOT Modification: disabled\n"
                            "A: summary L: Snapshot status B: back\n");
#if YW1_COMPANION_SALTY_ORIGINAL_EGL
                std::printf("R: SaltyNX Original EGL\n");
#endif
                return;
            }
            std::printf("PID: %llu Expected Title ID: %016llX\nInitial Main Full Build ID Verified: %s\n"
                        "Before PID/Title/Main: %08X/%08X/%08X\n"
                        "After PID/Title/Main: %08X/%08X/%08X\n",
                        static_cast<unsigned long long>(observation.pid),
                        static_cast<unsigned long long>(yw1::diagnostics::kTitleId),
                        targetBoolean(got.mainVerified), snapshot.beforePid, snapshot.beforeTitle,
                        snapshot.beforeMain, snapshot.afterPid, snapshot.afterTitle,
                        snapshot.afterMain);
            if (got.rtldKnown && got.memory.regionValid) {
                const auto relative = relativeAddress(got.memory.region.base, got.rtldBase);
                std::printf("RTLD: %016llX Region/RTLD: %s0x%llX\n",
                            static_cast<unsigned long long>(got.rtldBase),
                            relative.negative ? "-" : "+",
                            static_cast<unsigned long long>(relative.magnitude));
            } else {
                std::printf("RTLD / Region Relative: unknown\n");
            }
#endif
            if (got.valueKnown) {
                const auto relative = relativeAddress(got.slotValue, got.mainBase);
                std::printf("Target: %016llX Main Relative: %s0x%llX\n",
                            static_cast<unsigned long long>(got.slotValue),
                            relative.negative ? "-" : "+",
                            static_cast<unsigned long long>(relative.magnitude));
            } else {
                std::printf("Target / Main Relative: unknown\n");
            }
            if (got.memory.regionValid && got.valueKnown) {
                std::printf("Region Offset: +0x%llX Type: %X Permission: %X Attributes: %X\n",
                            static_cast<unsigned long long>(got.slotValue - got.memory.region.base),
                            got.memory.region.type, got.memory.region.permission,
                            got.memory.region.attributes);
            } else {
                std::printf("Region Offset / Type / Permission / Attributes: unknown\n");
            }
            const auto stage = [](const char* name, std::uint32_t value) {
                std::printf("%s: %08X%s\n", name, value,
                            value == UINT32_MAX ? " (not attempted)" : "");
            };
            stage("Preflight", snapshot.preflight);
            stage("Revalidation", snapshot.revalidation);
            stage("Map", snapshot.map);
            stage("Read", snapshot.read);
            stage("Unmap", snapshot.unmap);
            stage("Postvalidation", snapshot.postvalidation);
            std::printf("Snapshot Valid: %s Quarantined: %s\nLength: %u\n",
                        targetBoolean(snapshot.bytesValid),
                        targetBoolean(snapshot.cleanupQuarantined), kSnapshotLength);
#if !YW1_COMPANION_EGL_CODE_ATTRIBUTION
            if (snapshot.bytesValid) {
                for (unsigned i = 0; i < kSnapshotLength; ++i) {
                    std::printf("%02X%s", snapshot.bytes[i],
                                i % 8 == 7 ? "\n" : (i % 4 == 3 ? " " : ""));
                }
            } else {
                std::printf("unknown\n");
            }
#else
            std::printf("R: HEX page (accepted bytes only)\n");
#endif
            std::printf("Consistency: %s\nNo target execution or branch following.\n"
                        "Export/GOT Hook Active: no GOT Modification: disabled\n"
                        "Snapshot does not verify owner or hook safety.\n"
                        "A: summary L: Target Memory B: back\n",
                        snapshotConsistency(snapshot));
        };
#endif
#endif
#endif
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
            const auto& egl = observation.egl;
#if YW1_BOOTSTRAP_SAFE_HOOK_DIAGNOSTIC
            const auto& hook = egl.installer;
#if YW1_COMPANION_EGL_GOT_PROBE
            std::printf("Static Guard: %08X (%s)\nGuard-derived Preflight: %08X\n"
                        "Installer Status: not linked; preflight NOT ATTEMPTED\n"
                        "Backend candidate: Export Trampoline (preparation only)\n"
                        "Trampoline Prepared: %08X Original Published: %08X\n"
                        "Thread Safety: unverified Commit Attempted: %u Active: %u\nBlocked Reason "
                        "(MOD): %08X\n",
                        egl.guardStatus,
                        egl.guardStatus == UINT32_MAX ? "not recorded" : "executed", hook.preflight,
                        hook.trampoline, hook.publish, hook.commitAttempted, hook.active,
                        hook.blockedReason);
#else
            std::printf("Installer Status (MOD): %u Selected Hook Backend: %u\n"
                        "Preflight: %08X Original Prepared: %08X\n"
                        "Thread Safety: %s Commit Attempted: %u\n"
                        "Blocked Reason (MOD): %08X Active: %u\n",
                        static_cast<unsigned>(hook.state), static_cast<unsigned>(hook.backend),
                        hook.preflight, hook.trampoline,
                        hook.quiescenceVerified ? "verified" : "unverified", hook.commitAttempted,
                        hook.blockedReason, hook.active);
#endif
#endif
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
            std::printf("Snapshot status (MOD): %08X\n"
#if YW1_EGL_CONTEXT_PROBE
                        "Legacy export hook disabled; Main hook: see Callback page\nA: summary "
#else
                        "Hook is disabled in this build\nA: summary "
#endif
                        "L: details B: back\n",
                        egl.snapshotStatus);
#if YW1_COMPANION_EGL_GOT_PROBE
            std::printf("R: EGL GOT Probe\n");
#endif
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
#if YW1_COMPANION_EGL_GOT_PROBE
#if YW1_COMPANION_EGL_TARGET_MEMORY
#if YW1_COMPANION_EGL_CODE_SNAPSHOT
                    if (codeSnapshotPage) {
                        details = true;
                    }
                    codeSnapshotPage = false;
#if YW1_COMPANION_EGL_CODE_ATTRIBUTION
                    codeSnapshotPart = 0;
#endif
#endif
                    if (targetMemoryPage) {
                        details = true;
                    }
                    targetMemoryPage = false;
#endif
                    if (gotPage) {
                        details = true;
                    }
                    gotPage = false;
#endif
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
#if YW1_COMPANION_EGL_GOT_PROBE
#if YW1_COMPANION_EGL_TARGET_MEMORY
#if YW1_COMPANION_EGL_CODE_SNAPSHOT
                else if (codeSnapshotPage) {
#if YW1_COMPANION_EGL_CODE_ATTRIBUTION
                    if ((down & HidNpadButton_R) && codeSnapshotPart <
#if YW1_COMPANION_SALTY_ORIGINAL_EGL
#if YW1_COMPANION_EGL_CALLBACK
#if YW1_EGL_CONTEXT_PROBE
                        5
#else
                        3
#endif
#else
                        2
#endif
#else
                        1
#endif
                    ) {
                        ++codeSnapshotPart;
                        showCodeSnapshot();
                    } else if ((down & HidNpadButton_L) && codeSnapshotPart > 0) {
                        --codeSnapshotPart;
                        showCodeSnapshot();
                    } else
#endif
                    if (down & HidNpadButton_L) {
                        codeSnapshotPage = false;
                        targetMemoryPage = true;
                        showTargetMemory();
                    }
                } else if (targetMemoryPage && (down & HidNpadButton_R)) {
                    targetMemoryPage = false;
                    codeSnapshotPage = true;
                    showCodeSnapshot();
                }
#endif
                else if (targetMemoryPage) {
                    if (down & HidNpadButton_L) {
                        targetMemoryPage = false;
                        gotPage = true;
                        showGot();
                    }
                } else if (gotPage && (down & HidNpadButton_R)) {
                    gotPage = false;
                    targetMemoryPage = true;
                    showTargetMemory();
                }
#endif
                else if (gotPage) {
                    if (down & HidNpadButton_L) {
                        gotPage = false;
                        eglPage = true;
                        showEgl();
                    }
                } else if (eglPage && (down & HidNpadButton_R)) {
                    eglPage = false;
                    gotPage = true;
                    showGot();
                }
#endif
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
