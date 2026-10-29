#pragma once
#include <cstdint>

namespace yw1::bootstrap {
void initializeContextDiagnostic();
void prepareContextDiagnostic();
#if YW1_NATIVE_RENDER_EXPERIMENT
void probeContextCallback(std::uintptr_t swapDisplay, std::uintptr_t swapSurface);
#else
void probeContextCallback();
#endif
} // 名前空間 yw1::bootstrap
