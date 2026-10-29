#include "render_report.hpp"
#include "report.hpp"
#include "render/gl_function_specs.hpp"
#include <utility>

namespace yw1::companion {
namespace {
using F = diagnostics::RenderField;
bool available(const Observation& o) {
    return o.renderReadResult == 0 && diagnostics::validRenderWire(
        o.render, YW1_RENDER_PROFILE, YW1_RENDER_MODE);
}
const char* functionName(std::uint64_t i) {
    return i < std::size(render::kGlFunctions) ? render::kGlFunctions[i].name : "none";
}
const char* modeName(std::uint64_t mode) {
    return mode == 1 ? "MINIMAL" : mode == 2 ? "DEMO" : mode == 3 ? "DIAGNOSTIC" : "OFF";
}
} // 無名名前空間
void appendRenderReport(Report& r, const Observation& o) {
#if YW1_RENDER_ACTIVATION_DIAGNOSTICS
    r.append("\n=== YW1 Renderer Activation B0.2.14 ABI4 ===\nrender_read_result=0x%08X\n",
#elif YW1_RENDER_MEMORY_DIAGNOSTICS
    r.append("\n=== YW1 Renderer Memory B0.2.13 ABI3 ===\nrender_read_result=0x%08X\n",
#else
    r.append("\n=== YW1 Renderer Foundation B0.2.12 ABI2 ===\nrender_read_result=0x%08X\n",
#endif
             o.renderReadResult);
    if (!available(o)) { r.append("render_state=unknown wrong_pair_abi_or_unstable\n"); return; }
    const auto& w = o.render;
    r.append("render_profile=%llu requested_mode=%s render_stage=%llu disable_reason=%s\n",
        static_cast<unsigned long long>(w[F::Profile]), modeName(w[F::Mode]),
        static_cast<unsigned long long>(w[F::Stage]), diagnostics::renderReasonName(w[F::DisableReason]));
    for (auto [name, field] : {
            std::pair{"render_callbacks",F::Callbacks}, {"render_skips",F::Skips},
            {"render_busy_skips",F::Busy}, {"render_context_checks",F::ContextChecks},
            {"render_context_changes",F::ContextChanges}, {"arena_initialized",F::ArenaInitialized},
            {"arena_capacity",F::ArenaCapacity}, {"arena_used",F::ArenaUsed}, {"arena_peak",F::ArenaPeak},
            {"arena_allocations",F::ArenaAllocations}, {"arena_frees",F::ArenaFrees},
            {"arena_failures",F::ArenaFailures}, {"arena_invalid_frees",F::ArenaInvalidFrees},
            {"loader_attempts",F::LoaderAttempts}, {"loader_required",F::GlRequired},
            {"loader_attempted",F::GlAttempted}, {"loader_verified",F::GlVerified},
            {"loader_rejected",F::GlRejected}, {"loader_result",F::GlResult},
            {"imgui_init_attempts",F::InitAttempts}, {"imgui_contexts",F::ImGuiContexts},
            {"imgui_new_frames",F::NewFrames}, {"draw_submitted",F::DrawSubmitted},
            {"shader_state",F::Shader}, {"font_state",F::Font}, {"buffer_state",F::Buffer},
            {"gl_state_saves",F::StateSaves}, {"gl_state_restores",F::StateRestores},
            {"gl_state_failures",F::StateFailures}, {"backend_failure",F::BackendFailure},
            {"memory_contract_proven",F::MemoryContractProven}}) {
        r.append("%s=%llu\n", name, static_cast<unsigned long long>(w[field]));
    }
    r.append("loader_first_failure=%s\nrender_context=0x%016llX render_thread=0x%016llX\n"
             "render_display=0x%016llX draw_surface=0x%016llX read_surface=0x%016llX\n"
             "render_api=0x%04llX thread_result=0x%08X binding_result=%llu\n"
#if YW1_RENDER_ACTIVATION_DIAGNOSTICS
             "resource_state_0=not_attempted hardware_visibility=unconfirmed GL_error=not_consumed\n",
#else
             "resource_state_0=not_attempted native_imgui_calls=blocked_before_allocation\n"
             "Draw=disabled Demo_visible=not_observed GL_error=not_consumed\n",
#endif
        functionName(w[F::GlFirstFailure]), static_cast<unsigned long long>(w[F::Context]),
        static_cast<unsigned long long>(w[F::Thread]), static_cast<unsigned long long>(w[F::Display]),
        static_cast<unsigned long long>(w[F::DrawSurface]),
        static_cast<unsigned long long>(w[F::ReadSurface]),
        static_cast<unsigned long long>(w[F::Api]), static_cast<unsigned>(w[F::ThreadResult]),
        static_cast<unsigned long long>(w[F::BindingResult]));
    r.append("loader_result_unattempted=0xFFFFFFFF state_scope=last_observation\n");
#if YW1_RENDER_MEMORY_DIAGNOSTICS
    r.append("native_memory_admitted=%llu native_memory_unproven_mask=0x%02llX\n"
             "memory_proof_domain=%s Host_fault_evidence=external_only\n"
             "memory_obligations=1:construction 2:vector_growth 4:font 8:demo\n"
             "arena_attempts=%llu arena_largest_allocation=%llu arena_last_failed_request=%llu\n"
             "arena_largest_free=%llu arena_reserved=%llu\n",
        static_cast<unsigned long long>(w[F::NativeMemoryAdmitted]),
        static_cast<unsigned long long>(w[F::NativeMemoryBlockMask]),
        w[F::Profile] == 3 ? "Native_unproven" : "not_requested",
        static_cast<unsigned long long>(w[F::ArenaAttempts]),
        static_cast<unsigned long long>(w[F::ArenaLargestAllocation]),
        static_cast<unsigned long long>(w[F::ArenaLastFailedRequest]),
        static_cast<unsigned long long>(w[F::ArenaLargestFree]),
        static_cast<unsigned long long>(w[F::ArenaReserved]));
#endif
    r.append("render_owner_context=0x%016llX render_owner_thread=0x%016llX\n",
        static_cast<unsigned long long>(w[F::OwnerContext]),
        static_cast<unsigned long long>(w[F::OwnerThread]));
    for (unsigned i = 0; i < std::size(render::kGlFunctions); ++i) {
        const auto& spec = render::kGlFunctions[i];
        const auto bit = std::uint64_t{1} << i;
        r.append("loader_function_%s=%s\n", spec.name, !spec.required ? "excluded_debug" :
            w[F::GlVerifiedMask] & bit ? "verified_last_attempt" :
            w[F::GlRejectedMask] & bit ? "rejected_last_attempt" : "not_attempted");
    }
}
void showRenderPage(const Observation& o, bool memoryPage) {
#if YW1_RENDER_ACTIVATION_DIAGNOSTICS
    std::printf("Renderer Activation B0.2.14 %s\nRead: %08X\n",
#elif YW1_RENDER_MEMORY_DIAGNOSTICS
    std::printf("Renderer Memory B0.2.13 %s\nRead: %08X\n",
#else
    std::printf("Renderer Foundation B0.2.12 %s\nRead: %08X\n",
#endif
#if YW1_RENDER_ACTIVATION_DIAGNOSTICS
                memoryPage ? "2/3" : "1/3", o.renderReadResult);
#else
                memoryPage ? "2/2" : "1/2", o.renderReadResult);
#endif
    if (!available(o)) { std::printf("Runtime state: unknown / wrong pair or ABI\n"); }
    else {
        const auto& w = o.render;
        std::printf("Profile: %llu Requested: %s Stage: %llu\nReason: %s\n",
            static_cast<unsigned long long>(w[F::Profile]), modeName(w[F::Mode]),
            static_cast<unsigned long long>(w[F::Stage]),
            diagnostics::renderReasonName(w[F::DisableReason]));
        if (memoryPage) {
            std::printf("Arena initialized: %llu Capacity: %llu bytes\n"
                        "Used / Peak: %llu / %llu bytes\nAlloc / Free / Fail / Invalid: %llu / %llu / %llu / %llu\n"
                        "Loader attempts: %llu Required: %llu\nVerified / Rejected: %llu / %llu\n"
                        "Loader result: %llu First failure: %s\nMemory contract proven: %llu\n",
                static_cast<unsigned long long>(w[F::ArenaInitialized]),
                static_cast<unsigned long long>(w[F::ArenaCapacity]),
                static_cast<unsigned long long>(w[F::ArenaUsed]), static_cast<unsigned long long>(w[F::ArenaPeak]),
                static_cast<unsigned long long>(w[F::ArenaAllocations]),
                static_cast<unsigned long long>(w[F::ArenaFrees]),
                static_cast<unsigned long long>(w[F::ArenaFailures]),
                static_cast<unsigned long long>(w[F::ArenaInvalidFrees]),
                static_cast<unsigned long long>(w[F::LoaderAttempts]),
                static_cast<unsigned long long>(w[F::GlRequired]),
                static_cast<unsigned long long>(w[F::GlVerified]),
                static_cast<unsigned long long>(w[F::GlRejected]),
                static_cast<unsigned long long>(w[F::GlResult]), functionName(w[F::GlFirstFailure]),
                static_cast<unsigned long long>(w[F::MemoryContractProven]));
#if YW1_RENDER_MEMORY_DIAGNOSTICS
            std::printf("Largest allocation: %llu Attempts: %llu\n"
                        "Largest free: %llu Reserved: %llu\n"
#if YW1_RENDER_ACTIVATION_DIAGNOSTICS
                        "Complete OOM admission: %llu Unproven mask: %02llX\n"
#else
                        "Native admission: %llu Unproven mask: %02llX\n"
#endif
                        "Host fault evidence: external / Native: %s\n",
                static_cast<unsigned long long>(w[F::ArenaLargestAllocation]),
                static_cast<unsigned long long>(w[F::ArenaAttempts]),
                static_cast<unsigned long long>(w[F::ArenaLargestFree]),
                static_cast<unsigned long long>(w[F::ArenaReserved]),
                static_cast<unsigned long long>(w[F::NativeMemoryAdmitted]),
                static_cast<unsigned long long>(w[F::NativeMemoryBlockMask]),
                w[F::Profile] == 3 ? "unproven" : "not requested");
#endif
        } else {
            std::printf("Callbacks / Skips / Busy: %llu / %llu / %llu\n"
                        "Context checks / changes: %llu / %llu\n"
                        "Last context: %016llX Thread: %016llX\n"
                        "ImGui init / Context / NewFrame / Draw: %llu / %llu / %llu / %llu\n"
                        "Shader / Font / Buffer: %llu / %llu / %llu (0=not attempted)\n"
                        "State save / restore / fail: %llu / %llu / %llu\n",
                static_cast<unsigned long long>(w[F::Callbacks]), static_cast<unsigned long long>(w[F::Skips]),
                static_cast<unsigned long long>(w[F::Busy]), static_cast<unsigned long long>(w[F::ContextChecks]),
                static_cast<unsigned long long>(w[F::ContextChanges]),
                static_cast<unsigned long long>(w[F::Context]), static_cast<unsigned long long>(w[F::Thread]),
                static_cast<unsigned long long>(w[F::InitAttempts]), static_cast<unsigned long long>(w[F::ImGuiContexts]),
                static_cast<unsigned long long>(w[F::NewFrames]), static_cast<unsigned long long>(w[F::DrawSubmitted]),
                static_cast<unsigned long long>(w[F::Shader]), static_cast<unsigned long long>(w[F::Font]),
                static_cast<unsigned long long>(w[F::Buffer]), static_cast<unsigned long long>(w[F::StateSaves]),
                static_cast<unsigned long long>(w[F::StateRestores]), static_cast<unsigned long long>(w[F::StateFailures]));
        }
        std::printf("Owner context: %016llX Thread: %016llX\n",
            static_cast<unsigned long long>(w[F::OwnerContext]),
            static_cast<unsigned long long>(w[F::OwnerThread]));
        if (o.callbackReadResult == 0 && diagnostics::validCallbackWire(o.callback)) {
            using C = diagnostics::CallbackField;
            std::printf("Callback / Original Call / Return: %llu / %llu / %llu\n",
                static_cast<unsigned long long>(o.callback[C::CallbackCount]),
                static_cast<unsigned long long>(o.callback[C::OriginalCalls]),
                static_cast<unsigned long long>(o.callback[C::OriginalReturns]));
            if (o.callback[C::OriginalReturns]) {
                std::printf("Last Original: %08X\n", static_cast<unsigned>(o.callback[C::LastOriginalResult]));
            }
        }
#if YW1_RENDER_ACTIVATION_DIAGNOSTICS
        std::printf("Draw policy: %s / Visibility: user observation\n",
                    w[F::ExperimentalEnabled] ? "Native experimental ON" : "OFF");
#else
        std::printf("Draw: disabled / Demo visible: not observed\n");
#endif
    }
    std::printf("A: summary L: previous %s B: back\n", memoryPage ?
#if YW1_RENDER_ACTIVATION_DIAGNOSTICS
        "R: Native runtime" : "R: memory / GL loader");
#else
        "" : "R: memory / GL loader");
#endif
}
} // 名前空間 yw1::companion
