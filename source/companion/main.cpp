#include "report.hpp"

#include <switch.h>

namespace {
constinit Result smResult = yw1::companion::kNotAttempted;
constinit Result appletResult = yw1::companion::kNotAttempted;
constinit Result hidResult = yw1::companion::kNotAttempted;
constinit Result fsResult = yw1::companion::kNotAttempted;
constinit yw1::companion::Report report;

// 保存失敗時も、全Resultを画面から回収できるように固定幅でページ分割する。
std::size_t showDetails(std::size_t page) {
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
    report.append("\n=== YW1 Diagnostics B0.1.5 ===\nsource=companion-nro\n"
                  "title_id=0100C0000CEEA000\n");
    report.result("sm_startup_result", smResult);
    report.result("applet_startup_result", appletResult);
    report.result("hid_startup_result", hidResult);
    report.result("fs_startup_result", fsResult);
    const bool screenReady = appletResult == 0 && consoleInit(nullptr) != nullptr;
    const bool album = appletResult == 0 && appletGetAppletType() == AppletType_LibraryApplet;
    report.append("album_library_applet=%s\n", album ? "true" : "false");
    Observation observation;
    if (album && smResult == 0) {
        observation = inspectTarget(report);
    } else {
        report.append("inspection_not_attempted=requires_album_library_applet\n");
    }
    report.append("module_detected=%d\ndt_init_reached=%d\ninspection_result=0x%08X\n",
                  observation.moduleDetected, observation.dtInitReached, observation.result);
    bool writerActive = false;
    if (observation.snapshotRead) {
        const auto& state = observation.snapshot;
        writerActive = state.ipcQuarantined != 0 ||
                       (state.results[17] == yw1::diagnostics::kPresent && state.results[57] == 0 &&
                        state.results[62] != (yw1::diagnostics::kPresent | 0xE002));
    }
    FileOutcome file;
    if (fsResult == 0 && !writerActive) {
        file = saveReport(report);
    } else {
        report.append("sd_save_not_attempted=%s\n",
                      writerActive ? "game_writer_active" : "fs_unavailable");
    }
    if (screenReady) {
        const auto summary = [&]() {
            consoleClear();
            std::printf("YW1 Diagnostics B0.1.5\nsource=companion-nro\n\n");
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
        summary();
        bool details = false;
        std::size_t page = 0;
        std::size_t lastPage = 0;
        PadState pad{};
        if (hidResult == 0) {
            padConfigureInput(1, HidNpadStyleSet_NpadStandard);
            padInitializeDefault(&pad);
        }
        while (appletMainLoop()) {
            if (hidResult == 0) {
                padUpdate(&pad);
                const auto down = padGetButtonsDown(&pad);
                if (down & HidNpadButton_B) {
                    break;
                }
                if (down & HidNpadButton_A) {
                    details = !details;
                    page = 0;
                    if (details) {
                        lastPage = showDetails(page);
                    } else {
                        summary();
                    }
                } else if (details && (down & HidNpadButton_L) && page != 0) {
                    lastPage = showDetails(--page);
                } else if (details && (down & HidNpadButton_R) && page < lastPage) {
                    lastPage = showDetails(++page);
                }
            }
            consoleUpdate(nullptr);
            svcSleepThread(16000000);
        }
        consoleExit(nullptr);
    }
    return 0;
}
