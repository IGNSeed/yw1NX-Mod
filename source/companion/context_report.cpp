#include "context_report.hpp"
#include "report.hpp"
#if YW1_EGL_GETTER_WHITELIST
#include "diagnostics/egl_hook_guard.hpp"
#endif
#include <tuple>

namespace yw1::companion {
namespace {
using F = diagnostics::ContextField;
using I = diagnostics::ContextItem;
using diagnostics::ValueState;
using diagnostics::valueStateName;
bool observed(ValueState state) {
    return state == ValueState::Observed || state == ValueState::Truncated;
}
bool available(const Observation& observation) {
    return observation.contextReadResult == 0 && diagnostics::validContextWire(observation.context);
}
const char* apiName(std::uint64_t api) {
    return api == 0x30A2 ? "Desktop OpenGL" : api == 0x30A0 ? "OpenGL ES"
        : api == 0x30A1 ? "OpenVG" : api == 0x3038 ? "EGL_NONE" : "unknown";
}
const char* missing(const Observation& observation) {
    return observation.b028Profile ? "B028 module: context publication not present"
        : observation.legacyModule ? "B021 module: context publication not present"
        : "unpublished, unstable, invalid or failed read";
}
} // 無名名前空間
void appendContextReport(Report& report, const Observation& observation) {
    report.append("\n=== YW1 EGL GL Context Probe %s ===\n"
                  "source=module-bootstrap transport=companion-nro\n"
                  "context_wire_read_result=0x%08X old_B028_profile=%u\n"
                  "gl_get_error_called=false egl_get_error_called=false\n"
                  "context_scope=first_bounded_sample_not_live_context_monitor\n",
                  diagnostics::kContextVersion, observation.contextReadResult, observation.b028Profile);
    if (!available(observation)) {
        report.append("context_observation=unknown reason=%s\n", missing(observation)); return;
    }
    const auto& w = observation.context;
#if YW1_EGL_GETTER_WHITELIST
    if (w[F::SubsdkBase]) {
        report.append("probe_subsdk0_base=0x%016llX probe_subsdk0_full_build_id=",
                      static_cast<unsigned long long>(w[F::SubsdkBase]));
        for (auto b : diagnostics::kSubsdk0Id) { report.append("%02X", b); }
        report.append("\nprobe_subsdk0_identity_verified=%s\n",
                      w[F::GuardResult] == 0 ? "true" : "false");
    } else { report.append("probe_subsdk0_base=unknown probe_subsdk0_full_build_id=unknown\n"); }
    report.append("probe_proc_pointer_policy=%s probe_getter_dispatch_mask=0x%llX\n",
                  diagnostics::kGetterVerificationPolicy,
                  static_cast<unsigned long long>(w[F::GetterDispatchMask]));
#endif
    report.append("context_probe_profile=%llu probe_attempted=%llu probe_attempts=%llu "
                  "probe_maximum_attempts=%llu\nprobe_status=%s probe_reason=%s\n"
                  "probe_guard_result=0x%08X probe_thread_result=0x%08X "
                  "probe_pointer_result=0x%08X\n"
                  "probe_sample_callback_count=%llu probe_retry_at=%llu "
                  "probe_completed=%llu\n"
                  "context_binding_valid=%llu context_binding_changed=%llu "
                  "api_validated_mask=0x%llX\n",
                  static_cast<unsigned long long>(w[F::Profile]),
                  static_cast<unsigned long long>(w[F::Attempted]),
                  static_cast<unsigned long long>(w[F::Attempts]),
                  static_cast<unsigned long long>(w[F::MaximumAttempts]),
                  diagnostics::probeStatusName(w[F::Status]), diagnostics::probeReasonName(w[F::Reason]),
                  static_cast<unsigned>(w[F::GuardResult]), static_cast<unsigned>(w[F::ThreadResult]),
                  static_cast<unsigned>(w[F::PointerResult]),
                  static_cast<unsigned long long>(w[F::SampleCallback]),
                  static_cast<unsigned long long>(w[F::RetryAt]),
                  static_cast<unsigned long long>(w[F::Completed]),
                  static_cast<unsigned long long>(w[F::BindingValid]),
                  static_cast<unsigned long long>(w[F::BindingChanged]),
                  static_cast<unsigned long long>(w[F::ApiValidatedMask]));
    if (w[F::PointerResult] != UINT32_MAX) {
        report.append("probe_gl_string_pointer=0x%016llX probe_gl_integer_pointer=0x%016llX\n"
                      "probe_pointer_verification_result=0x%08X\n",
                      static_cast<unsigned long long>(w[F::GlStringPointer]),
                      static_cast<unsigned long long>(w[F::GlIntegerPointer]),
                      static_cast<unsigned>(w[F::PointerResult]));
#if YW1_EGL_GETTER_WHITELIST
        for (auto [label, field] : {std::pair{"probe_gl_string_relative_offset", F::GlStringPointer},
                                    {"probe_gl_integer_relative_offset", F::GlIntegerPointer}}) {
            if (w[F::SubsdkBase] && w[field] >= w[F::SubsdkBase]) {
                report.append("%s=0x%llX\n", label,
                    static_cast<unsigned long long>(w[field] - w[F::SubsdkBase]));
            } else { report.append("%s=unknown_pointer_outside_module\n", label); }
        }
#else
        report.append("probe_proc_pointer_policy=%s\n", diagnostics::kGetterVerificationPolicy);
#endif
    } else { report.append("probe_gl_string_pointer=not_attempted probe_gl_integer_pointer=not_attempted\n"); }
    for (auto [label, item, field] : {
            std::tuple{"egl_current_api", I::Api, F::CurrentApi},
            {"egl_current_context", I::Context, F::Context},
            {"egl_current_display", I::Display, F::Display},
            {"egl_draw_surface", I::DrawSurface, F::DrawSurface},
            {"egl_read_surface", I::ReadSurface, F::ReadSurface},
            {"probe_render_thread", I::Thread, F::ThreadId}}) {
        const auto state = w.state(item);
        report.append("%s_state=%s", label, valueStateName(state));
        if (observed(state)) { report.append(" %s=0x%016llX", label,
            static_cast<unsigned long long>(w[field])); }
        report.append("\n");
    }
    report.append("egl_api_name=%s\n", observed(w.state(I::Api)) ? apiName(w[F::CurrentApi])
                                                                      : "not_attempted");
    const auto text = [&](const char* name, I item, const auto& value) {
        report.append("%s_state=%s\n", name, valueStateName(w.state(item)));
        if (observed(w.state(item))) { report.append("%s=%s\n", name, value.data()); }
#if YW1_EGL_GETTER_WHITELIST
        const auto index = static_cast<unsigned>(item) - static_cast<unsigned>(I::Version);
        report.append("%s_result=0x%08X %s_copy_length=%u\n", name,
            diagnostics::contextValueResult(w, item), name, diagnostics::contextStringLength(w, index));
        if (!observed(w.state(item))) {
            report.append("%s=unknown unavailable_reason=%s\n", name,
                w.state(item) == ValueState::Failed ? "null_unreadable_or_invalid_string"
                                                  : valueStateName(w.state(item)));
        }
#endif
    };
    text("probe_gl_version", I::Version, w.version); text("probe_gl_vendor", I::Vendor, w.vendor);
    text("probe_gl_renderer", I::Renderer, w.renderer); text("probe_glsl_version", I::Glsl, w.glsl);
    for (auto [name, item, offset, length] : {
            std::tuple{"probe_gl_viewport", I::Viewport, 0u, 4u},
            {"probe_gl_framebuffer", I::Framebuffer, 4u, 1u},
            {"probe_gl_program", I::Program, 5u, 1u},
            {"probe_gl_major_minor", I::VersionIntegers, 6u, 2u},
            {"probe_gl_profile_mask", I::Profile, 8u, 1u}}) {
        report.append("%s_state=%s", name, valueStateName(w.state(item)));
        if (observed(w.state(item))) {
            report.append(" %s=", name);
            for (unsigned i = 0; i < length; ++i) {
                report.append("%s%d", i ? "," : "", w.integers[offset + i]);
            }
        }
        report.append("\n");
#if YW1_EGL_GETTER_WHITELIST
        report.append("%s_result=0x%08X\n", name, diagnostics::contextValueResult(w, item));
        if (!observed(w.state(item))) {
            report.append("%s=unknown unavailable_reason=%s\n", name,
                w.state(item) == ValueState::Failed ? "output_unwritten_or_inconsistent"
                                                  : valueStateName(w.state(item)));
        }
#endif
    }
    report.append("probe_query_semantics=observed_output_not_GL_error_verified\n"
                  "probe_snapshot_consistency=two_equal_samples_even_generation\n"
                  "post_capture_context_changes=not_monitored_no_cached_pointer_calls\n");
#if YW1_EGL_GETTER_WHITELIST
    report.append("probe_query_result_namespace=mod_output_validation_not_native_GL_Result\n"
                  "GL_error_state=not_consumed_not_checked Draw=disabled ImGui=disabled\n");
#endif
}
void showContextPage(const Observation& o, bool glPage) {
    std::printf("EGL / GL Context %s %s\nSource: game module  Transport: NRO\n"
                "Read Result: %08X\n", diagnostics::kContextVersion,
                glPage ? "2/2" : "1/2", o.contextReadResult);
    if (!available(o)) { std::printf("Context data: unknown\n%s\n", missing(o)); }
    else {
        const auto& w = o.context;
        std::printf("Profile: %llu  Attempted: %llu  Attempts: %llu/3\nStatus: %s\nReason: %s\n",
                    static_cast<unsigned long long>(w[F::Profile]),
                    static_cast<unsigned long long>(w[F::Attempted]),
                    static_cast<unsigned long long>(w[F::Attempts]),
                    diagnostics::probeStatusName(w[F::Status]),
                    diagnostics::probeReasonName(w[F::Reason]));
        if (!glPage) {
            std::printf("Guard: %08X Thread: %08X Pointer: %08X\nSample Callback: %llu\n",
                static_cast<unsigned>(w[F::GuardResult]), static_cast<unsigned>(w[F::ThreadResult]),
                static_cast<unsigned>(w[F::PointerResult]),
                static_cast<unsigned long long>(w[F::SampleCallback]));
            for (auto [name, item, field] : {
                    std::tuple{"Context", I::Context, F::Context},
                    {"Display", I::Display, F::Display}, {"Draw Surface", I::DrawSurface, F::DrawSurface},
                    {"Read Surface", I::ReadSurface, F::ReadSurface},
                    {"Render Thread", I::Thread, F::ThreadId}, {"Current API", I::Api, F::CurrentApi}}) {
                if (observed(w.state(item))) { std::printf("%s: %016llX\n", name,
                    static_cast<unsigned long long>(w[field])); }
                else { std::printf("%s: %s\n", name, valueStateName(w.state(item))); }
            }
            std::printf("API: %s Binding valid: %llu Changed: %llu\n",
                observed(w.state(I::Api)) ? apiName(w[F::CurrentApi]) : "not_attempted",
                static_cast<unsigned long long>(w[F::BindingValid]),
                static_cast<unsigned long long>(w[F::BindingChanged]));
            if (w[F::PointerResult] != UINT32_MAX) {
                std::printf("GL String / Integer Proc: %016llX / %016llX\n",
                    static_cast<unsigned long long>(w[F::GlStringPointer]),
                    static_cast<unsigned long long>(w[F::GlIntegerPointer]));
            }
        } else {
            for (auto [name, item, value] : {
                    std::tuple{"GL Version", I::Version, w.version.data()},
                    {"Vendor", I::Vendor, w.vendor.data()}, {"Renderer", I::Renderer, w.renderer.data()},
                    {"GLSL", I::Glsl, w.glsl.data()}}) {
                std::printf("%s [%s]: %.70s\n", name, valueStateName(w.state(item)),
                            observed(w.state(item)) ? value : "unknown");
            }
            for (auto [name, item, offset, length] : {
                    std::tuple{"Viewport", I::Viewport, 0u, 4u},
                    {"Framebuffer", I::Framebuffer, 4u, 1u}, {"Program", I::Program, 5u, 1u},
                    {"GL Major/Minor", I::VersionIntegers, 6u, 2u},
                    {"Profile Mask", I::Profile, 8u, 1u}}) {
                std::printf("%s: %s", name, valueStateName(w.state(item)));
                if (observed(w.state(item))) {
                    for (unsigned i = 0; i < length; ++i) { std::printf(" %d", w.integers[offset + i]); }
                }
                std::printf("\n");
            }
            std::printf("Read queries; GL error state not consumed/checked.\n");
#if YW1_EGL_GETTER_WHITELIST
            std::printf("Copied bytes Version/Vendor/Renderer/GLSL: %u/%u/%u/%u\n",
                diagnostics::contextStringLength(w, 0), diagnostics::contextStringLength(w, 1),
                diagnostics::contextStringLength(w, 2), diagnostics::contextStringLength(w, 3));
#endif
        }
    }
    if (o.callbackReadResult == 0) {
        using C = diagnostics::CallbackField;
        std::printf("Callback / Original Call / Return: %llu / %llu / %llu\n",
            static_cast<unsigned long long>(o.callback[C::CallbackCount]),
            static_cast<unsigned long long>(o.callback[C::OriginalCalls]),
            static_cast<unsigned long long>(o.callback[C::OriginalReturns]));
    }
    std::printf("EGL/GL GetError: never called. Draw/ImGui: disabled\n"
                "A: summary L: previous %s B: back\n", glPage ?
#if YW1_RENDER_DIAGNOSTICS
                "R: Renderer"
#elif YW1_IMGUI_DEMO_DIAGNOSTICS
                "R: Demo status"
#else
                ""
#endif
                : "R: GL page");
}
} // 名前空間 yw1::companion
