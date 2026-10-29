#pragma once
#include "diagnostics/context_probe.hpp"
#include "game/symbols/symbol_registry.hpp"

namespace yw1::bootstrap {
void prepareRenderDiagnostic(diagnostics::ContextPlatform& platform,
    const diagnostics::ContextApi& api, const game::ModuleImage& module,
    std::uint64_t textBase, std::uint64_t textSize);
#if YW1_NATIVE_RENDER_EXPERIMENT
void probeRenderCallback(std::uintptr_t swapDisplay, std::uintptr_t swapSurface);
#else
void probeRenderCallback();
#endif
} // 名前空間 yw1::bootstrap
