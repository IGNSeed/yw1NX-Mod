#include "render_wire.hpp"
#include "render/gl_function_specs.hpp"
#include <bit>

namespace yw1::diagnostics {
#if YW1_RENDER_MODULE
extern "C" {
#if YW1_RENDER_PROFILE == 3
alignas(4096) constinit RenderPublication yw1_render_diagnostics{YW1_RENDER_PROFILE, YW1_RENDER_MODE};
#else
extern constinit const RenderPublication yw1_render_diagnostics{YW1_RENDER_PROFILE, YW1_RENDER_MODE};
#endif
}
#if YW1_RENDER_PROFILE == 3
void publishRender(const RenderWire& wire) {
    auto& p = yw1_render_diagnostics;
    const auto previous = std::atomic_ref(p.generation).fetch_add(1, std::memory_order_acq_rel);
    for (unsigned i = 0; i < wire.words.size(); ++i) {
        std::atomic_ref(p.words[i]).store(wire.words[i], std::memory_order_release);
    }
    std::atomic_ref(p.generation).store(previous + 2, std::memory_order_release);
}
#endif
#endif
bool validRenderWire(const RenderWire& w, unsigned expectedProfile, unsigned expectedMode) {
    using F = RenderField;
    if (expectedProfile < 1 || expectedProfile > 3 || expectedMode > 2 ||
        (expectedProfile != 3 && expectedMode != 0) || (expectedProfile == 3 && expectedMode == 0) ||
        w[F::Magic] != kRenderMagic || w[F::Abi] != kRenderAbi || w[F::Size] != sizeof(w) ||
        w[F::BuildTag] != kRenderTag || w[F::Profile] != expectedProfile || w[F::Mode] != expectedMode ||
        w[F::Stage] > static_cast<unsigned>(RenderStage::Admission) ||
        w[F::DisableReason] > static_cast<unsigned>(RenderDisable::OomUnproven) ||
        w[F::ArenaInitialized] > 1 || w[F::GlRequired] != 60 || w[F::GlVerified] > 60 ||
        w[F::GlAttempted] > 60 || w[F::GlRejected] > 60 ||
        (w[F::GlResult] > 6 && w[F::GlResult] != UINT32_MAX) ||
        w[F::GlAttempted] != w[F::GlVerified] + w[F::GlRejected] ||
        (w[F::BindingResult] > 6 && w[F::BindingResult] != UINT32_MAX) ||
        w[F::Api] > UINT32_MAX || w[F::LoaderAttempts] > 1 ||
        w[F::ContextChecks] > w[F::Callbacks] || w[F::Skips] != w[F::Callbacks] ||
        w[F::ArenaUsed] > w[F::ArenaPeak] || w[F::ArenaPeak] > w[F::ArenaCapacity] ||
        w[F::ArenaFrees] > w[F::ArenaAllocations] || w[F::ThreadResult] > UINT32_MAX ||
        (w[F::GlFirstFailure] != UINT32_MAX && w[F::GlFirstFailure] >= 61)) { return false; }
    // 実機の任意OOM契約は未成立。この版のModuleが描画したという記録は受け付けない。
    for (auto f : {F::InitAttempts, F::ImGuiContexts, F::NewFrames, F::DrawSubmitted,
                   F::Shader, F::Font, F::Buffer, F::StateSaves, F::StateRestores,
                   F::StateFailures, F::BackendFailure, F::MemoryContractProven,
                   F::PointerCallsAllowed}) {
        if (w[f]) { return false; }
    }
    constexpr auto requiredMask = [] {
        std::uint64_t mask = 0;
        for (unsigned i = 0; i < std::size(render::kGlFunctions); ++i) {
            if (render::kGlFunctions[i].required) { mask |= std::uint64_t{1} << i; }
        }
        return mask;
    }();
    if ((w[F::GlVerifiedMask] | w[F::GlRejectedMask]) & ~requiredMask ||
        (w[F::GlVerifiedMask] & w[F::GlRejectedMask]) ||
        std::popcount(w[F::GlVerifiedMask]) != w[F::GlVerified] ||
        std::popcount(w[F::GlRejectedMask]) != w[F::GlRejected] ||
        (!w[F::LoaderAttempts] && w[F::GlResult] != UINT32_MAX)) { return false; }
    for (unsigned i = static_cast<unsigned>(F::Reserved53); i < w.words.size(); ++i) {
        if (w.words[i]) { return false; }
    }
    if (expectedProfile != 3) { return w == initialRenderWire(expectedProfile, 0); }
    if (w[F::ArenaInitialized] && (w[F::ArenaCapacity] < 65536 || w[F::ArenaCapacity] > 4 * 1024 * 1024)) {
        return false;
    }
    if (w[F::DisableReason] == static_cast<unsigned>(RenderDisable::OomUnproven) &&
        (!w[F::ArenaInitialized] || w[F::GlVerified] != 60 || w[F::GlResult] != 0)) { return false; }
    return true;
}
const char* renderReasonName(std::uint64_t value) {
    constexpr const char* names[]{"profile_disabled", "none", "concurrent_or_reentrant", "identity",
        "context_unavailable", "different_thread", "binding_changed", "context_lost", "arena",
        "gl_resolver_rejected", "arbitrary_imgui_oom_unproven"};
    return value < std::size(names) ? names[value] : "invalid";
}
} // 名前空間 yw1::diagnostics
