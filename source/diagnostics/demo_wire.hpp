#pragma once

#include <array>
#include <cstdint>

namespace yw1::diagnostics {
constexpr std::uint64_t kDemoMagic = 0x31314449315759, kDemoAbi = 1, kDemoTag = 0xB00211;
enum class DemoField : unsigned {
    Magic, Abi, Size, BuildTag, Profile, Requested, Blockers, InitAttempts, InitSuccess,
    DrawAttempts, DrawSuccess, DrawEnabled, Shader, Font, Buffer, StateSaved, StateRestored,
    ContextMonitoring, LoaderValidated, ResourceLifetime, LastStage, Reserved21
};
enum DemoBlocker : std::uint64_t {
    AllocatorContract = 1, FailureStateRestore = 2, DrawingFunctionDispatch = 4,
    ContextResourceLifetime = 8
};
constexpr std::uint64_t kDemoBlockers = AllocatorContract | FailureStateRestore |
    DrawingFunctionDispatch | ContextResourceLifetime;
struct DemoWire {
    std::array<std::uint64_t, 32> words{};
    constexpr std::uint64_t& operator[](DemoField f) { return words[static_cast<unsigned>(f)]; }
    constexpr std::uint64_t operator[](DemoField f) const { return words[static_cast<unsigned>(f)]; }
    constexpr bool operator==(const DemoWire&) const = default;
};
static_assert(sizeof(DemoWire) == 256);
constexpr DemoWire initialDemoWire(unsigned profile) {
    DemoWire w;
    using F = DemoField;
    w[F::Magic] = kDemoMagic; w[F::Abi] = kDemoAbi; w[F::Size] = sizeof(w);
    w[F::BuildTag] = kDemoTag; w[F::Profile] = profile; w[F::Requested] = profile == 3;
    w[F::Blockers] = kDemoBlockers;
    // 1はRendererを呼ぶ前の停止。0の各状態は未実施で、成功を意味しない。
    w[F::LastStage] = profile == 3 ? 1 : 0;
    return w;
}
constexpr bool validDemoWire(const DemoWire& w, unsigned expectedProfile) {
    if (expectedProfile < 1 || expectedProfile > 3) { return false; }
    // 描画停止版に成功状態を混ぜた記録や別ABIを受理しない。
    return w == initialDemoWire(expectedProfile);
}
extern "C" const DemoWire yw1_demo_diagnostics;
} // 名前空間 yw1::diagnostics
