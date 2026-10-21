#include <common.hpp>
#include <lib/util/sys/cur_proc_handle.hpp>

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
    if (R_FAILED(result) || handle == INVALID_HANDLE || handle > 0xFFFFFFFFu) {
        return INVALID_HANDLE;
    }
    processHandle = static_cast<Handle>(handle);
    return processHandle;
}

} // 名前空間 exl::util::proc_handle
