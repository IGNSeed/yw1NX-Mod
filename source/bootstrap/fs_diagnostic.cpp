#include "logging/progress.hpp"
#include "logging/sd_writer.hpp"

#include <common.hpp>
#include <lib/util/sys/cur_proc_handle.hpp>
#if YW1_BOOTSTRAP_EXTERNAL_DIAGNOSTICS
#include "diagnostics/log_export.hpp"
#endif

namespace yw1::bootstrap {
namespace {

constexpr u64 kTitleId = 0x0100C0000CEEA000;
// 固定MesosphereのPseudoHandle定義。所有する実handleとして閉じない。
constexpr Handle kCurrentProcess = 0xFFFF8001;
constexpr Handle kCurrentThread = 0xFFFF8000;

template <std::size_t Size> void event(const char (&message)[Size]) {
    static_cast<void>(logging::memoryLog().append(message, Size - 1));
    static_cast<void>(svcOutputDebugString(message, Size - 1));
}

template <std::size_t Size> void resultEvent(const char (&label)[Size], Result result) {
    static_cast<void>(logging::memoryLog().appendResult(label, Size - 1, result));
}

} // 無名名前空間

void runFsDiagnostic() {
    logging::recordProgress(logging::Stage::InitEnter, 0);
#if YW1_BOOTSTRAP_EXTERNAL_DIAGNOSTICS
    diagnostics::setExportBootToken(svcGetSystemTick());
    event("\n=== YW1 B0.1.6 boot / FS diagnostic only ===\n");
    event("source=module-bootstrap\n");
#else
    event("\n=== YW1 B0.1.4 boot / FS diagnostic only ===\n");
#endif
    event("[YW1] init_enter; profile=fs-only; exlaunch=not_requested\n");
    u64 titleId = 0;
    auto result = svcGetInfo(&titleId, InfoType_ProgramId, kCurrentProcess, 0);
    logging::recordProgress(logging::Stage::ProgramId, result);
    logging::recordObservedTitleId(titleId);
    resultEvent("[YW1] program_id_pseudo_result=", result);
    // 照会成功時の対象不一致を、別の照会で上書きして通過させない。
    if (result == 0 && titleId != kTitleId) {
        logging::recordProgress(logging::Stage::GuardDenied, logging::kWrongEntry);
        event("[YW1] program_id_mismatch; worker_skipped\n");
        logging::recordWriterTerminal(3);
        return;
    }

    // 実handle取得は診断項目であり、pseudo照会成功時の保存開始条件にはしない。
    const auto handle = exl::util::proc_handle::Get();
    resultEvent("[YW1] process_handle_result=", logging::progress.processHandleResult.load());
    if (result != 0 && handle != INVALID_HANDLE) {
        titleId = 0;
        result = svcGetInfo(&titleId, InfoType_ProgramId, handle, 0);
        resultEvent("[YW1] program_id_real_handle_result=", result);
    }
    logging::recordProgress(logging::Stage::ProgramId, result);
    logging::recordObservedTitleId(titleId);
    if (result != 0 || titleId != kTitleId) {
        logging::recordProgress(logging::Stage::GuardDenied,
                                result != 0 ? result : logging::kWrongEntry);
        event("[YW1] program_id_unverified; worker_skipped\n");
        logging::recordWriterTerminal(3);
        return;
    }
    event("[YW1] program_id_verified\n");

    std::uint32_t priority = 59;
    const auto priorityResult = svcGetThreadPriority(&priority, kCurrentThread);
    logging::recordProgress(logging::Stage::PriorityQuery, priorityResult);
    resultEvent("[YW1] caller_priority_query_result=", priorityResult);
    // 対象NPDMの許容28..59内で起動元と同じ優先度。取得失敗時は従来値を保つ。
    if (priorityResult != 0 || priority < 28 || priority > 59) {
        priority = 59;
    }
    resultEvent("[YW1] worker_selected_priority=", static_cast<Result>(priority));
    event("[YW1] init_return; independent worker eligible\n");
    logging::recordProgress(logging::Stage::InitReturn, 0);
    logging::startSdLogger(static_cast<int>(priority));
}

} // 名前空間 yw1::bootstrap
