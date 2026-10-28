#include "context_wire.hpp"
#include <cstring>

namespace yw1::diagnostics {
extern "C" { constinit ContextPublication yw1_context_diagnostics{}; }
ContextWire initialContextWire(unsigned profile) {
    ContextWire wire;
    using F = ContextField;
    wire[F::Magic] = kContextMagic; wire[F::Abi] = kContextAbi;
    wire[F::Size] = sizeof(wire); wire[F::BuildTag] = kContextTag;
    wire[F::Profile] = profile; wire[F::MaximumAttempts] = 3;
    wire[F::GuardResult] = wire[F::ThreadResult] = wire[F::PointerResult] = UINT32_MAX;
    wire[F::Status] = static_cast<unsigned>(profile == 1 ? ProbeStatus::Disabled
                                                       : ProbeStatus::NotAttempted);
    wire[F::Reason] = static_cast<unsigned>(profile == 1 ? ProbeReason::ReadOnly
#if YW1_EGL_GETTER_WHITELIST
                                                       : ProbeReason::NoCallbackObserved);
    wire[F::GetterPolicy] = 1;
#else
                                                       : ProbeReason::HookUnavailable);
#endif
    return wire;
}
void publishContext(const ContextWire& wire) {
    std::array<std::uint64_t, kContextWords> words{};
    std::memcpy(words.data(), &wire, sizeof(wire));
    auto& p = yw1_context_diagnostics;
    const auto before = p.generation.fetch_add(1, std::memory_order_acq_rel);
    for (unsigned i = 0; i < kContextWords; ++i) {
        p.words[i].store(words[i], std::memory_order_release);
    }
    p.generation.store(before + 2, std::memory_order_release);
}
bool validContextWire(const ContextWire& wire) {
    using F = ContextField;
    const auto terminated = [](const auto& text) {
        for (char c : text) { if (c == 0) { return true; } }
        return false;
    };
    if (wire[F::Magic] != kContextMagic || wire[F::Abi] != kContextAbi ||
        wire[F::Size] != sizeof(wire) || wire[F::BuildTag] != kContextTag ||
        wire[F::Profile] < 1 || wire[F::Profile] > 2 || wire[F::Status] > 4 ||
        wire[F::Reason] > static_cast<unsigned>(
#if YW1_EGL_GETTER_WHITELIST
            ProbeReason::SnapshotCompleted) ||
#else
            ProbeReason::QueryUnwritten) ||
#endif
        wire[F::Attempted] > 1 || wire[F::Attempts] > 3 || wire[F::MaximumAttempts] != 3 ||
        wire[F::BindingValid] > 1 || wire[F::BindingChanged] > 1 || wire[F::Completed] > 1 ||
        wire[F::GlErrorConsumed] != 0 || wire[F::EglErrorConsumed] != 0 ||
        wire[F::ApiValidatedMask] > 127 || wire[F::CurrentApi] > UINT32_MAX ||
        wire[F::GuardResult] > UINT32_MAX || wire[F::ThreadResult] > UINT32_MAX ||
        wire[F::PointerResult] > UINT32_MAX ||
        !terminated(wire.version) || !terminated(wire.vendor) ||
        !terminated(wire.renderer) || !terminated(wire.glsl)) { return false; }
    for (auto state : wire.states) { if (static_cast<unsigned>(state) > 4) { return false; } }
    if (wire[F::Attempted] != (wire[F::Attempts] != 0) ||
        (wire[F::Attempted] && wire[F::SampleCallback] == 0) ||
        (wire[F::Profile] == 1 && (wire[F::Attempted] || wire[F::BindingValid] ||
                                 wire[F::Completed] || wire[F::ApiValidatedMask])) ||
        (wire[F::BindingValid] && (wire[F::Context] == 0 || wire[F::Display] == 0)) ||
        (wire[F::Completed] &&
         (wire[F::Status] != static_cast<unsigned>(ProbeStatus::Captured) ||
          !wire[F::BindingValid] || wire[F::ApiValidatedMask] != 127 ||
          wire.state(ContextItem::Version) != ValueState::Observed || wire.version[0] == 0)) ||
        (wire.state(ContextItem::Thread) == ValueState::Observed &&
         (wire[F::ThreadResult] != 0 || wire[F::ThreadId] == 0)) ||
        (wire.state(ContextItem::Viewport) == ValueState::Observed &&
         (wire.integers[2] < 0 || wire.integers[3] < 0))) {
        return false;
    }
#if YW1_EGL_GETTER_WHITELIST
    if (wire[F::GetterPolicy] != 1 || wire[F::GetterDispatchMask] > 3 ||
        (wire[F::SubsdkBase] != 0 && wire[F::SubsdkBase] % 4096) || wire.reserved[1] != 0) {
        return false;
    }
    constexpr std::uint16_t maximum[]{255, 127, 127, 255};
    for (unsigned i = 0; i < 4; ++i) {
        if (contextStringLength(wire, i) > maximum[i]) { return false; }
    }
    const auto lengthMatches = [&](const auto& text, unsigned index) {
        unsigned length = 0;
        while (length < text.size() && text[length] != 0) { ++length; }
        return length == contextStringLength(wire, index);
    };
    if (!lengthMatches(wire.version, 0) || !lengthMatches(wire.vendor, 1) ||
        !lengthMatches(wire.renderer, 2) || !lengthMatches(wire.glsl, 3) ||
        (wire[F::Completed] && (wire[F::GetterDispatchMask] != 3 ||
                                wire[F::CurrentApi] != 0x30A2))) { return false; }
    return wire.state(ContextItem::Reserved) == ValueState::NotAttempted &&
        wire.integers[9] == 0 && wire.integers[10] == 0 && wire.integers[11] == 0;
#else
    for (unsigned i = static_cast<unsigned>(F::Reserved29); i < wire.words.size(); ++i) {
        if (wire.words[i]) { return false; }
    }
    return wire.reserved[0] == 0 && wire.reserved[1] == 0 &&
        wire.state(ContextItem::Reserved) == ValueState::NotAttempted &&
        wire.integers[9] == 0 && wire.integers[10] == 0 && wire.integers[11] == 0;
#endif
}
const char* probeStatusName(std::uint64_t value) {
    constexpr const char* names[]{"not_attempted", "disabled", "waiting_context",
                                  "captured_error_not_checked", "failed"};
    return value < 5 ? names[value] : "invalid";
}
const char* probeReasonName(std::uint64_t value) {
    [[maybe_unused]] constexpr const char* names[]{"none", "read_only_profile", "hook_unavailable", "identity",
        "invalid_function_pointer", "tls_not_ready", "unsupported_dispatch_error_side_effect",
        "thread_id_unavailable", "different_thread", "no_context", "invalid_binding",
        "unsupported_client_api", "gl_function_not_verified", "invalid_version",
        "binding_changed", "query_output_not_observed"};
#if YW1_EGL_GETTER_WHITELIST
    constexpr const char* current[]{"none", "read_only_profile", "hook_unavailable", "identity",
        "invalid_function_pointer", "tls_not_ready", "unsupported_dispatch_error_side_effect",
        "thread_id_unavailable", "different_thread", "no_context", "context_not_current",
        "api_not_desktop_gl", "gl_function_not_verified", "unsupported_value_query",
        "context_not_current", "gl_integer_call_failed", "no_callback_observed",
        "context_not_current", "api_not_desktop_gl", "gl_string_call_failed",
        "gl_integer_call_failed", "unsupported_value_query", "snapshot_completed"};
    return value < std::size(current) ? current[value] : "invalid";
#else
    return value < std::size(names) ? names[value] : "invalid";
#endif
}
const char* valueStateName(ValueState state) {
    constexpr const char* names[]{"not_attempted", "unsupported", "failed", "observed",
                                  "truncated"};
    const auto value = static_cast<unsigned>(state);
    return value < 5 ? names[value] : "invalid";
}
#if YW1_EGL_GETTER_WHITELIST
std::uint32_t contextValueResult(const ContextWire& wire, ContextItem item) {
    switch (wire.state(item)) {
    case ValueState::NotAttempted: return UINT32_MAX;
    case ValueState::Unsupported: return 0xE294;
    case ValueState::Failed: return 0xE293;
    case ValueState::Observed: case ValueState::Truncated: return 0;
    }
    return 0xE295;
}
std::uint16_t contextStringLength(const ContextWire& wire, unsigned index) {
    return index < 4 ? static_cast<std::uint16_t>(wire.reserved[0] >> (index * 16)) : 0;
}
#endif
} // 名前空間 yw1::diagnostics
