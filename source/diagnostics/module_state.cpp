#include "module_state.hpp"

namespace yw1::diagnostics {

#if defined(__GNUC__)
#define YW1_RETAIN __attribute__((used))
#else
#define YW1_RETAIN
#endif

// 非ゼロの初期値を持ち、DT_INIT前にも存在する。BSSの初期化には依存しない。
extern "C" {
YW1_RETAIN alignas(64) constinit const ModuleIdentity yw1_module_identity{
    kIdentityMagic,
    kAbiVersion,
    sizeof(ModuleIdentity),
    kTitleId,
    kBuildTag,
    {0x7F, 0x35, 0xBD, 0xFC, 0x5D, 0xE4, 0x6C, 0xF1, 0xED, 0xE9,
     0xC0, 0x61, 0x8B, 0x47, 0x04, 0x7B, 0xAF, 0xF4, 0x9E, 0x12}};
YW1_RETAIN alignas(4096) constinit ModuleDiagnostics yw1_module_diagnostics_state{};
}

void recordModuleProgress(std::uint32_t stage, std::uint32_t result) {
    auto& state = yw1_module_diagnostics_state;
    if (stage >= kStageCount) {
        return;
    }
    const auto value = (static_cast<std::uint64_t>(stage) << 32) | result;
    state.results[stage].store(kPresent | result, std::memory_order_release);
    if (result != 0 && result != UINT32_MAX) {
        state.lastNonzero.store(value, std::memory_order_release);
        std::uint64_t empty = 0;
        static_cast<void>(state.firstNonzero.compare_exchange_strong(empty, value));
        empty = 0;
        static_cast<void>(
            state.firstNonzeroByStage[stage].compare_exchange_strong(empty, kPresent | result));
    }
    state.latest.store(value, std::memory_order_release);
}

} // 名前空間 yw1::diagnostics
