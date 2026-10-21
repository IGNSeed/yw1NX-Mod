#include <common.hpp>
#include <lib/util/sys/cur_proc_handle.hpp>

#include <atomic>

extern "C" void exl_module_init();
extern "C" Result __real_svcMapProcessMemory(void*, Handle, u64, u64);
extern "C" Result __real_svcUnmapProcessMemory(void*, Handle, u64, u64);

namespace {

constexpr u64 kTitleId = 0x0100C0000CEEA000;
std::atomic<bool> initEntered{false};
std::atomic<bool> mainEntered{false};

template <std::size_t Size> void writeLog(const char (&message)[Size]) {
    // 出力の成功は実機ログを取得するまで認定しない。
    static_cast<void>(svcOutputDebugString(message, Size - 1));
}

void writeResult(const char* label, std::size_t length, Result result) {
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
    writeLog("[YW1 B0.1] init_enter; EGL hooks disabled\n");
    const Handle handle = exl::util::proc_handle::Get();
    if (handle == INVALID_HANDLE) {
        writeLog("[YW1 B0.1] process_handle_unavailable; initialization skipped\n");
        return;
    }
    u64 titleId = 0;
    const Result result = svcGetInfo(&titleId, InfoType_ProgramId, handle, 0);
    if (R_FAILED(result) || titleId != kTitleId) {
        writeLog("[YW1 B0.1] program_id_mismatch_or_query_failed; initialization skipped\n");
        return;
    }
    writeLog("[YW1 B0.1] process_handle_ready; entering pinned exlaunch initialization\n");
    // ここから先の上流RwPages等の失敗はabortになり得るため、実機判定は保留する。
    exl_module_init();
    writeLog("[YW1 B0.1] init_return; original game startup may continue\n");
}

extern "C" void exl_main(void*, void*) {
    if (!mainEntered.exchange(true)) {
        writeLog("[YW1 B0.1] exl_main_reached; no hooks, rendering, or input changes\n");
    }
}

extern "C" Result __wrap_svcMapProcessMemory(void* destination, Handle process, u64 source,
                                             u64 size) {
    // Module内のリンク時ラップだけで、ゲームのSVCや命令を書き換えない。
    writeLog("[YW1 B0.1] RwPages map begin\n");
    const Result result = __real_svcMapProcessMemory(destination, process, source, size);
    constexpr char kLabel[] = "[YW1 B0.1] RwPages map result=";
    writeResult(kLabel, sizeof(kLabel) - 1, result);
    return result;
}

extern "C" Result __wrap_svcUnmapProcessMemory(void* destination, Handle process, u64 source,
                                               u64 size) {
    const Result result = __real_svcUnmapProcessMemory(destination, process, source, size);
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
