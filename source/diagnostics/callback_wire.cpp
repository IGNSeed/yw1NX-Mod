#include "callback_wire.hpp"

namespace yw1::diagnostics {
extern "C" { constinit CallbackPublication yw1_callback_diagnostics{}; }
namespace {
using Field = CallbackField;
auto& slot(Field field) {
    return yw1_callback_diagnostics.words[static_cast<unsigned>(field)];
}
bool counter(Field field) {
    return field == Field::CallbackCount || field == Field::OriginalCalls ||
           field == Field::OriginalReturns || field == Field::LastOriginalResult ||
           field == Field::ReentryCount || field == Field::InFlight;
}
} // 無名名前空間
CallbackWire initialCallbackWire(CallbackProfile profile) {
    CallbackWire wire;
    wire[Field::Magic] = kCallbackMagic;
    wire[Field::Abi] = kCallbackAbi;
    wire[Field::Size] = sizeof(wire);
    wire[Field::BuildTag] = kCallbackBuildTag;
    wire[Field::Profile] = static_cast<unsigned>(profile);
    wire[Field::Method] = 2;
    for (auto field : {Field::InstallResult, Field::GuardResult, Field::LastOriginalResult,
                       Field::RollbackResult, Field::MappingResult, Field::CacheResult,
                       Field::CleanupResult, Field::StubResult}) {
        wire[field] = UINT32_MAX;
    }
    wire[Field::InitializationStage] = 1;
    return wire;
}
void initializeCallbackWire(CallbackProfile profile) {
    const auto wire = initialCallbackWire(profile);
    // 初期化一回だけ。Hook公開後にはcounterを初期化しない。
    yw1_callback_diagnostics.generation.store(1, std::memory_order_release);
    for (unsigned i = 0; i < kCallbackWords; ++i) {
        yw1_callback_diagnostics.words[i].store(wire.words[i], std::memory_order_release);
    }
    yw1_callback_diagnostics.generation.store(2, std::memory_order_release);
}
void publishCallbackConfiguration(const CallbackWire& wire) {
    auto& publication = yw1_callback_diagnostics;
    const auto previous = publication.generation.fetch_add(1, std::memory_order_acq_rel);
    for (unsigned i = 0; i < kCallbackWords; ++i) {
        if (!counter(static_cast<Field>(i))) {
            publication.words[i].store(wire.words[i], std::memory_order_release);
        }
    }
    publication.generation.store(previous + 2, std::memory_order_release);
}
void recordCallbackEnter() {
    slot(Field::CallbackCount).fetch_add(1, std::memory_order_relaxed);
    if (slot(Field::InFlight).fetch_add(1, std::memory_order_relaxed) != 0) {
        // TLSへ依存せず、再入または他threadとの重なりを記録する。
        slot(Field::ReentryCount).fetch_add(1, std::memory_order_relaxed);
    }
    slot(Field::OriginalCalls).fetch_add(1, std::memory_order_relaxed);
}
void recordCallbackReturn(std::uint32_t result) {
    slot(Field::LastOriginalResult).store(result, std::memory_order_relaxed);
    slot(Field::OriginalReturns).fetch_add(1, std::memory_order_relaxed);
    slot(Field::InFlight).fetch_sub(1, std::memory_order_relaxed);
}
bool validCallbackWire(const CallbackWire& wire) {
    for (auto field : {Field::InstallResult, Field::GuardResult, Field::LastOriginalResult,
                       Field::RollbackResult, Field::MappingResult, Field::CacheResult,
                       Field::CleanupResult, Field::StubResult, Field::HookError}) {
        if (wire[field] > UINT32_MAX) { return false; }
    }
    if (wire[Field::Profile] == 1 && (wire[Field::InstallAttempted] != 0 ||
        wire[Field::OriginalPublished] != 0 || wire[Field::HookActive] != 0 ||
        wire[Field::CallbackCount] != 0 || wire[Field::PatchAttempted] != 0)) {
        return false;
    }
    if (wire[Field::CallbackCount] != 0 && wire[Field::OriginalPublished] == 0) { return false; }
    return wire[Field::Magic] == kCallbackMagic && wire[Field::Abi] == kCallbackAbi &&
           wire[Field::Size] == sizeof(wire) && wire[Field::BuildTag] == kCallbackBuildTag &&
           wire[Field::Profile] >= 1 && wire[Field::Profile] <= 2 && wire[Field::Method] == 2 &&
           wire[Field::InstallAttempted] <= 1 && wire[Field::OriginalPublished] <= 1 &&
           wire[Field::HookActive] <= 2 && wire[Field::ThreadKnown] <= 1 &&
           wire[Field::RollbackAttempted] <= 1 && wire[Field::PatchAttempted] <= 1 &&
           wire[Field::ResourcesRetained] <= 1 && wire[Field::ThreadPolicy] <= 1 &&
           wire[Field::InitializationStage] <= 6 &&
           wire[Field::OriginalReturns] <= wire[Field::OriginalCalls] &&
           wire[Field::OriginalCalls] <= wire[Field::CallbackCount] &&
           (wire[Field::HookActive] == 0 || (wire[Field::OriginalPublished] == 1 &&
            wire[Field::PatchAttempted] == 1 && wire[Field::ThreadPolicy] == 1));
}
const char* callbackActivity(const CallbackWire& wire) {
    if (wire[Field::CallbackCount] != 0) { return "callback_observed"; }
    if (wire[Field::HookActive] == 2) { return "installation_state_uncertain"; }
    if (wire[Field::HookActive] == 1) {
        return "installed_zero_count_game_activity_unknown";
    }
    return wire[Field::OriginalPublished] ? "restored_or_failed_code_retained"
                                        : "hook_not_installed";
}
const char* callbackReason(const CallbackWire& wire) {
    const auto failed = [&](Field field) { return wire[field] != 0 && wire[field] != UINT32_MAX; };
    if (wire[Field::GuardResult] == UINT32_MAX) { return "initialization_not_reached"; }
    if (failed(Field::GuardResult)) { return "identity_instruction_range_or_memory_guard_rejected"; }
    if (wire[Field::Profile] == 1) { return "read_only_profile_no_install_requested"; }
    if (failed(Field::StubResult)) { return "original_path_preparation_failed"; }
    if (failed(Field::MappingResult)) { return "target_alias_mapping_failed"; }
    if (wire[Field::RollbackAttempted] && failed(Field::RollbackResult)) {
        return "rollback_failed_callback_code_retained";
    }
    if (failed(Field::CleanupResult)) { return "alias_cleanup_failed"; }
    if (failed(Field::CacheResult)) { return "cache_stage_failed"; }
    if (failed(Field::InstallResult)) { return "install_stopped_or_conflicting_instruction"; }
    return wire[Field::HookActive] == 1 ? "installed" : "not_committed";
}
} // 名前空間 yw1::diagnostics
