#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#if YW1_RENDER_MEMORY_DIAGNOSTICS
#include "render/memory_contract.hpp"
#endif

namespace yw1::diagnostics {
constexpr std::uint64_t kRenderMagic = 0x32314449315759;
#if YW1_RENDER_MEMORY_DIAGNOSTICS
constexpr std::uint64_t kRenderAbi = 3, kRenderTag = 0xB00213;
#else
constexpr std::uint64_t kRenderAbi = 2, kRenderTag = 0xB00212;
#endif
enum class RenderField : unsigned {
    Magic, Abi, Size, BuildTag, Profile, Mode, Stage, DisableReason, Callbacks, Skips, Busy,
    ContextChecks, ContextChanges, Context, Display, DrawSurface, ReadSurface, Thread, Api,
    ArenaInitialized, ArenaCapacity, ArenaUsed, ArenaPeak, ArenaAllocations, ArenaFrees,
    ArenaFailures, ArenaInvalidFrees, LoaderAttempts, GlRequired, GlAttempted, GlVerified,
    GlRejected, GlFirstFailure, GlResult, InitAttempts, ImGuiContexts, NewFrames, DrawSubmitted,
    Shader, Font, Buffer, StateSaves, StateRestores, StateFailures, BackendFailure,
    MemoryContractProven, PointerCallsAllowed, ThreadResult, BindingResult,
    GlVerifiedMask, GlRejectedMask, OwnerContext, OwnerThread,
#if YW1_RENDER_MEMORY_DIAGNOSTICS
    ArenaAttempts, ArenaLargestAllocation, ArenaLastFailedRequest, ArenaLargestFree,
    ArenaReserved, NativeMemoryBlockMask, NativeMemoryAdmitted, Reserved60
#else
    Reserved53
#endif
};
enum class RenderStage : std::uint64_t { Disabled, Waiting, Context, Allocator, Resolver, Admission };
enum class RenderDisable : std::uint64_t {
    Profile, None, Busy, Identity, Context, Thread, Changed, Lost, Arena, Resolver, OomUnproven
};
struct RenderWire {
    std::array<std::uint64_t, 64> words{};
    constexpr std::uint64_t& operator[](RenderField f) { return words[static_cast<unsigned>(f)]; }
    constexpr std::uint64_t operator[](RenderField f) const { return words[static_cast<unsigned>(f)]; }
    constexpr bool operator==(const RenderWire&) const = default;
};
static_assert(sizeof(RenderWire) == 512);
constexpr RenderWire initialRenderWire(unsigned profile, unsigned mode) {
    RenderWire w;
    using F = RenderField;
    w[F::Magic] = kRenderMagic; w[F::Abi] = kRenderAbi;
    w[F::Size] = sizeof(w); w[F::BuildTag] = kRenderTag;
    w[F::Profile] = profile; w[F::Mode] = mode;
    w[F::Stage] = static_cast<unsigned>(profile == 3 ? RenderStage::Waiting : RenderStage::Disabled);
    w[F::DisableReason] = static_cast<unsigned>(profile == 3 ? RenderDisable::None : RenderDisable::Profile);
    w[F::GlRequired] = 60; w[F::GlFirstFailure] = UINT32_MAX; w[F::ThreadResult] = UINT32_MAX;
    w[F::GlResult] = UINT32_MAX; w[F::BindingResult] = UINT32_MAX;
#if YW1_RENDER_MEMORY_DIAGNOSTICS
    w[F::NativeMemoryBlockMask] = render::nativeMemoryBlockMask(mode);
#endif
    return w;
}
struct RenderPublication {
    std::uint64_t generation = 2;
    std::array<std::uint64_t, 64> words{};
    constexpr RenderPublication(unsigned profile, unsigned mode)
        : words(initialRenderWire(profile, mode).words) {}
};
static_assert(sizeof(RenderPublication) == 520);
#if YW1_RENDER_MODULE && YW1_RENDER_PROFILE != 3
extern "C" const RenderPublication yw1_render_diagnostics;
#else
extern "C" RenderPublication yw1_render_diagnostics;
#endif
void publishRender(const RenderWire& wire);
bool validRenderWire(const RenderWire& wire, unsigned expectedProfile, unsigned expectedMode);
const char* renderReasonName(std::uint64_t value);
} // 名前空間 yw1::diagnostics
