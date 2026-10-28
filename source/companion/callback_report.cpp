#include "callback_report.hpp"
#include "report.hpp"

namespace yw1::companion {
void appendCallbackReport(Report& report, const Observation& observation) {
    using Field = diagnostics::CallbackField;
    report.append("\n=== YW1 EGL Callback Canary B0.2.8 ===\n"
                  "source=module-bootstrap transport=companion-nro\n"
                  "callback_read_result=0x%08X legacy_module=%s\n",
                  observation.callbackReadResult, observation.legacyModule ? "true" : "false");
    if (observation.callbackReadResult != 0) {
        report.append("callback_observation=unknown_or_legacy_abi_without_callback_publication\n"
                      "callback_count=unknown original_call_count=unknown hook_active=unknown\n");
        return;
    }
    const auto& wire = observation.callback;
    report.append("callback_abi=%llu callback_size=%llu callback_profile=%llu\n"
                  "hook_method=main_single_bl_to_bl hook_target=0x%016llX\n"
                  "original_plt=0x%016llX original_path=existing_PLT_GOT_SaltyNX_Original\n"
                  "install_attempted=%llu guard_result=0x%08X install_result=0x%08X\n"
                  "original_published=%llu hook_active=%llu patch_attempted=%llu\n"
                  "callback_count=%llu original_call_count=%llu original_return_count=%llu\n",
                  static_cast<unsigned long long>(wire[Field::Abi]),
                  static_cast<unsigned long long>(wire[Field::Size]),
                  static_cast<unsigned long long>(wire[Field::Profile]),
                  static_cast<unsigned long long>(wire[Field::Target]),
                  static_cast<unsigned long long>(wire[Field::Original]),
                  static_cast<unsigned long long>(wire[Field::InstallAttempted]),
                  static_cast<unsigned>(wire[Field::GuardResult]),
                  static_cast<unsigned>(wire[Field::InstallResult]),
                  static_cast<unsigned long long>(wire[Field::OriginalPublished]),
                  static_cast<unsigned long long>(wire[Field::HookActive]),
                  static_cast<unsigned long long>(wire[Field::PatchAttempted]),
                  static_cast<unsigned long long>(wire[Field::CallbackCount]),
                  static_cast<unsigned long long>(wire[Field::OriginalCalls]),
                  static_cast<unsigned long long>(wire[Field::OriginalReturns]));
    if (wire[Field::OriginalReturns] != 0) {
        report.append("last_original_result=0x%08X\n",
                      static_cast<unsigned>(wire[Field::LastOriginalResult]));
    } else { report.append("last_original_result=unknown\n"); }
    if (wire[Field::ThreadKnown]) {
        report.append("render_thread_id=%llu\n", static_cast<unsigned long long>(wire[Field::ThreadId]));
    } else { report.append("render_thread_id=unknown\n"); }
    report.append("reentry_or_overlap_count=%llu hook_error=0x%08X\n"
                  "rollback_attempted=%llu rollback_result=0x%08X initialization_stage=%llu\n"
                  "mapping_result=0x%08X cache_result=0x%08X stub_result=0x%08X cleanup_result=0x%08X\n"
                  "thread_policy=%s\n"
                  "cache_result_semantics=mod_stage_completed_instruction_sequence_not_native_Result\n"
                  "callback_activity=%s game_execution_state=unknown\ncallback_reason=%s\n"
                  "callback_consistency=two_equal_samples_even_configuration_generation\n"
                  "original_code_lifetime=process_stale_BL_allowed_after_restore\n"
#if YW1_EGL_CONTEXT_PROBE
                  "callback_fs_io=disabled callback_heap_allocation=disabled "
                  "GL=bounded_read_probe_only ImGui=disabled\n",
#else
                  "callback_fs_io=disabled callback_heap_allocation=disabled GL=disabled ImGui=disabled\n",
#endif
                  static_cast<unsigned long long>(wire[Field::ReentryCount]),
                  static_cast<unsigned>(wire[Field::HookError]),
                  static_cast<unsigned long long>(wire[Field::RollbackAttempted]),
                  static_cast<unsigned>(wire[Field::RollbackResult]),
                  static_cast<unsigned long long>(wire[Field::InitializationStage]),
                  static_cast<unsigned>(wire[Field::MappingResult]),
                  static_cast<unsigned>(wire[Field::CacheResult]),
                  static_cast<unsigned>(wire[Field::StubResult]),
                  static_cast<unsigned>(wire[Field::CleanupResult]),
                  wire[Field::ThreadPolicy] == 1 ? "atomic_direct_BL_not_thread_quiescence"
                                                : "unverified",
                  diagnostics::callbackActivity(wire), diagnostics::callbackReason(wire));
}
} // 名前空間 yw1::companion
