#include <common.hpp>
#include <lib/util/sys/cur_proc_handle.hpp>

#if YW1_BOOTSTRAP_SD_LOGGING || YW1_BOOTSTRAP_MEMORY_LOG
#include "logging/progress.hpp"
#endif

namespace exl::util::proc_handle {
namespace {

// Module初期化中だけ使用し、ゲームのTLSとIPCバッファを変更しない。
Handle processHandle = INVALID_HANDLE;

} // 無名名前空間

Handle Get() {
    if (processHandle != INVALID_HANDLE) {
        return processHandle;
    }
    u64 handle = 0;
    const Result result = svcGetInfo(&handle, InfoType_MesosphereCurrentProcess, INVALID_HANDLE, 0);
#if YW1_BOOTSTRAP_SD_LOGGING || YW1_BOOTSTRAP_MEMORY_LOG
    yw1::logging::progress.processHandleResult.store(result, std::memory_order_release);
    yw1::logging::recordProgress(yw1::logging::Stage::ProcessHandle, result);
#endif
    if (R_FAILED(result) || handle == INVALID_HANDLE || handle > 0xFFFFFFFFu) {
        return INVALID_HANDLE;
    }
    processHandle = static_cast<Handle>(handle);
    return processHandle;
}

} // 名前空間 exl::util::proc_handle
