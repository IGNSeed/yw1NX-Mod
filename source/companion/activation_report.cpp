#include "activation_report.hpp"
#include "report.hpp"
#include <utility>

namespace yw1::companion {
namespace {
using F = diagnostics::RenderField;
bool valid(const Observation& o) {
    return o.renderReadResult == 0 &&
           diagnostics::validRenderWire(o.render, YW1_RENDER_PROFILE, YW1_RENDER_MODE);
}
}
void appendActivationReport(Report& r, const Observation& o) {
    r.append("\n=== YW1 Native ImGui B0.2.14 ABI4 ===\n");
    if (!valid(o)) {
        r.append("native_state=unknown_pair_abi_or_read_failure\n");
        return;
    }
    r.append("render_build_tag=0x%06llX render_wire_abi=%llu\n",
             static_cast<unsigned long long>(o.render[F::BuildTag]),
             static_cast<unsigned long long>(o.render[F::Abi]));
    for (auto [name, field] : {std::pair{"native_imgui_linked", F::NativeLinked},
                               {"experimental_render_enabled", F::ExperimentalEnabled},
                               {"normal_path_experimental_admission", F::NormalPathAdmission},
                               {"complete_oom_contract_proven", F::MemoryContractProven},
                               {"allocator_type", F::AllocatorType},
                               {"imgui_context_live", F::ContextLive},
                               {"opengl_backend_initialized", F::BackendInitialized},
                               {"imgui_render_calls", F::RenderCalls},
                               {"last_render_result", F::LastRenderResult},
                               {"init_failures", F::InitFailures},
                               {"renderer_disabled", F::RendererDisabled},
                               {"arena_live_allocations", F::ArenaLive},
                               {"viewport_width", F::ViewportWidth},
                               {"viewport_height", F::ViewportHeight},
                               {"clock_frequency", F::ClockFrequency},
                               {"frame_time_ns", F::LastFrameTimeNs},
                               {"swap_binding_mismatches", F::BindingMismatch},
                               {"stack_available", F::StackAvailable}}) {
        r.append("%s=%llu\n", name, static_cast<unsigned long long>(o.render[field]));
    }
    r.append("resource_states=0:not_attempted 1:ready 2:failed\n"
             "allocator_type_1=private_module_arena hardware_visibility=requires_user_observation\n"
             "swap_display=0x%016llX swap_surface=0x%016llX\n",
             static_cast<unsigned long long>(o.render[F::SwapDisplay]),
             static_cast<unsigned long long>(o.render[F::SwapSurface]));
}
void showActivationPage(const Observation& o) {
    std::printf("Native ImGui B0.2.14 3/3\nRead: %08X\n", o.renderReadResult);
    if (!valid(o)) {
        std::printf("Unknown / wrong pair or ABI\n");
        return;
    }
    const auto& w = o.render;
    const auto q = [&](F f) { return static_cast<unsigned long long>(w[f]); };
    std::printf("Tag: %06llX Profile: %llu Mode: %llu\n", q(F::BuildTag), q(F::Profile), q(F::Mode));
    std::printf(
        "Native link / Experimental enabled: %llu / %llu\n"
        "Normal-path admission: %llu Complete OOM proof: %llu\n"
        "Allocator: %s Live allocations: %llu\n"
        "ImGui Context live / GL backend ready: %llu / %llu\n"
        "Init attempts / failures: %llu / %llu\n"
        "NewFrame / Render / Draw submissions: %llu / %llu / %llu\n"
        "Shader / Font / Buffer: %llu / %llu / %llu\n"
        "Resource: 0=not attempted 1=ready 2=failed\n"
        "State save / restored / failed: %llu / %llu / %llu\n"
        "Last render / Backend failure: %llu / %llu\n"
        "Disabled: %llu Reason: %s\n"
        "Viewport: %llu x %llu Clock Hz: %llu\n"
        "Swap binding mismatch: %llu Stack available: %llu\n"
        "Draw counts are API submissions. Visibility: user check\n"
        "A: summary L: memory B: back\n",
        q(F::NativeLinked), q(F::ExperimentalEnabled), q(F::NormalPathAdmission),
        q(F::MemoryContractProven), w[F::AllocatorType] == 1 ? "Module arena" : "not attempted",
        q(F::ArenaLive), q(F::ContextLive), q(F::BackendInitialized), q(F::InitAttempts),
        q(F::InitFailures), q(F::NewFrames), q(F::RenderCalls), q(F::DrawSubmitted), q(F::Shader),
        q(F::Font), q(F::Buffer), q(F::StateSaves), q(F::StateRestores), q(F::StateFailures),
        q(F::LastRenderResult), q(F::BackendFailure), q(F::RendererDisabled),
        diagnostics::renderReasonName(w[F::DisableReason]), q(F::ViewportWidth),
        q(F::ViewportHeight), q(F::ClockFrequency), q(F::BindingMismatch), q(F::StackAvailable));
}
} // 名前空間 yw1::companion
