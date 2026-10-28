#include "demo_report.hpp"
#include "report.hpp"

namespace yw1::companion {
namespace {
using D = diagnostics::DemoField;
bool available(const Observation& o) {
    return o.demoReadResult == 0 && diagnostics::validDemoWire(o.demo, YW1_DEMO_PROFILE);
}
const char* profileName(std::uint64_t p) {
    return p == 1 ? "A read-only" : p == 2 ? "B GL snapshot" : "C experimental drawing OFF";
}
} // 無名名前空間
void appendDemoReport(Report& r, const Observation& o) {
    r.append("\n=== ImGui Demo B0.2.11 drawing-stop diagnostics ===\n"
             "demo_wire_read_result=0x%08X\n", o.demoReadResult);
    if (!available(o)) { r.append("demo_observation=unknown\n"); return; }
    const auto& w = o.demo;
    r.append("demo_profile=%llu (%s) demo_requested=%llu demo_blockers=0x%llX\n"
             "demo_allocator=unverified demo_failure_state_restore=unverified\n"
             "demo_drawing_dispatch=unverified demo_context_resource_lifetime=unverified\n"
             "imgui_init_attempts=0 imgui_init_success=0 imgui_init_failure=not_attempted\n"
             "shader=not_attempted font=not_attempted buffer=not_attempted\n"
             "draw_attempts=0 draw_success=0 Draw=disabled\n"
             "demo_visible=not_observed no_draw_submitted=true\n"
             "gl_state_saved=not_attempted gl_state_restored=not_attempted\n"
             "context_change_monitor=not_implemented context_scope=B0210_first_snapshot\n"
             "last_render_stage=%s failure_reason=%s\n",
             static_cast<unsigned long long>(w[D::Profile]), profileName(w[D::Profile]),
             static_cast<unsigned long long>(w[D::Requested]),
             static_cast<unsigned long long>(w[D::Blockers]),
             w[D::Requested] ? "blocked_before_renderer" : "profile_disabled",
             w[D::Requested] ? "allocator_restore_dispatch_lifetime_unverified" : "profile_disabled");
    if (o.callbackReadResult == 0 && diagnostics::validCallbackWire(o.callback)) {
        using C = diagnostics::CallbackField;
        r.append("demo_callback_count=%llu demo_original_call=%llu demo_original_return=%llu\n",
                 static_cast<unsigned long long>(o.callback[C::CallbackCount]),
                 static_cast<unsigned long long>(o.callback[C::OriginalCalls]),
                 static_cast<unsigned long long>(o.callback[C::OriginalReturns]));
        if (o.callback[C::OriginalReturns]) {
            r.append("demo_original_last_result=0x%08X original_result_observed=true\n",
                     static_cast<unsigned>(o.callback[C::LastOriginalResult]));
        } else {
            r.append("demo_original_last_result=not_observed original_result_observed=false\n");
        }
        if (w[D::Requested]) {
            r.append("demo_skips_derived_from_callback=%llu skip_reason=renderer_not_linked\n",
                     static_cast<unsigned long long>(o.callback[C::CallbackCount]));
        } else { r.append("demo_skips=not_applicable_profile_disabled\n"); }
    } else { r.append("demo_callback_original_counters=unknown demo_skips=unknown\n"); }
    if (o.contextReadResult == 0 && diagnostics::validContextWire(o.context)) {
        using C = diagnostics::ContextField;
        r.append("demo_snapshot_context=0x%016llX demo_snapshot_thread=0x%016llX "
                 "demo_snapshot_binding_changed=%llu demo_snapshot_getter_mask=0x%llX\n",
                 static_cast<unsigned long long>(o.context[C::Context]),
                 static_cast<unsigned long long>(o.context[C::ThreadId]),
                 static_cast<unsigned long long>(o.context[C::BindingChanged]),
                 static_cast<unsigned long long>(o.context[C::GetterDispatchMask]));
    } else { r.append("demo_snapshot_identity=unknown\n"); }
}
void showDemoPage(const Observation& o) {
    std::printf("ImGui Demo B0.2.11 - DRAWING OFF\nRead: %08X\n", o.demoReadResult);
    if (!available(o)) { std::printf("Demo diagnostics: unknown / wrong pair or ABI\n"); }
    else {
        const auto& w = o.demo;
        std::printf("Profile: %s\nRequested: %llu Blockers: %02llX\n"
                    "Init attempts / success: 0 / 0 (not attempted)\n"
                    "Draw attempts / success: 0 / 0\n"
                    "Shader / Font / Buffer: not attempted\n"
                    "State save / restore: not attempted\n"
                    "Last stage: %s\n"
                    "Missing: allocator, failure state restore,\n"
                    "         drawing dispatch, context resource lifetime\n"
                    "Draw: disabled / Demo visible: not observed\n",
                    profileName(w[D::Profile]), static_cast<unsigned long long>(w[D::Requested]),
                    static_cast<unsigned long long>(w[D::Blockers]),
                    w[D::Requested] ? "blocked before renderer" : "profile disabled");
        if (o.contextReadResult == 0 && diagnostics::validContextWire(o.context)) {
            using C = diagnostics::ContextField;
            std::printf("Snapshot context: %016llX Thread: %016llX\n"
                        "Snapshot binding changed: %llu Getter mask: %llX\n",
                static_cast<unsigned long long>(o.context[C::Context]),
                static_cast<unsigned long long>(o.context[C::ThreadId]),
                static_cast<unsigned long long>(o.context[C::BindingChanged]),
                static_cast<unsigned long long>(o.context[C::GetterDispatchMask]));
        } else { std::printf("Snapshot context / thread: unknown\n"); }
        std::printf("Context changes after snapshot: NOT MONITORED\n");
        if (o.callbackReadResult == 0 && diagnostics::validCallbackWire(o.callback)) {
            using C = diagnostics::CallbackField;
            std::printf("Callback / Original Call / Return: %llu / %llu / %llu\n",
                static_cast<unsigned long long>(o.callback[C::CallbackCount]),
                static_cast<unsigned long long>(o.callback[C::OriginalCalls]),
                static_cast<unsigned long long>(o.callback[C::OriginalReturns]));
            if (o.callback[C::OriginalReturns]) {
                std::printf("Last Original result: %08X\n",
                    static_cast<unsigned>(o.callback[C::LastOriginalResult]));
            } else { std::printf("Last Original result: not observed\n"); }
            if (w[D::Requested]) {
                std::printf("Skips (derived from callback): %llu\n",
                    static_cast<unsigned long long>(o.callback[C::CallbackCount]));
            }
        } else { std::printf("Callback / Original / skips: unknown\n"); }
    }
    std::printf("A: summary L: GL snapshot B: back\n");
}
} // 名前空間 yw1::companion
