#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace yw1::diagnostics {
constexpr std::uint64_t kContextMagic = 0x394C4745315759;
#if YW1_EGL_GETTER_WHITELIST
constexpr std::uint64_t kContextAbi = 2, kContextTag = 0xB00210;
constexpr char kContextVersion[] = "B0.2.10";
constexpr char kGetterVerificationPolicy[] = "full_build_id_two_gl_stub_whitelist_and_dispatch";
#else
constexpr std::uint64_t kContextAbi = 1, kContextTag = 0xB0029;
constexpr char kContextVersion[] = "B0.2.9";
constexpr char kGetterVerificationPolicy[] = "exact_verified_subsdk0_entry_only";
#endif
enum class ContextField : unsigned {
    Magic, Abi, Size, BuildTag, Profile, Status, Reason, Attempted, Attempts, GuardResult,
    ThreadResult, SampleCallback, ThreadId, Context, Display, DrawSurface, ReadSurface,
    CurrentApi, BindingValid, BindingChanged, RetryAt, MaximumAttempts, PointerResult,
    ApiValidatedMask, Completed, GlErrorConsumed, EglErrorConsumed, GlStringPointer, GlIntegerPointer,
    Reserved29, Reserved30, Reserved31,
    SubsdkBase = Reserved29, GetterPolicy = Reserved30, GetterDispatchMask = Reserved31
};
enum class ProbeStatus : std::uint64_t { NotAttempted, Disabled, WaitingContext, Captured, Failed };
enum class ProbeReason : std::uint64_t {
    None, ReadOnly, HookUnavailable, Identity, InvalidPointer, TlsNotReady,
    UnsupportedDispatch, ThreadUnavailable, DifferentThread, NoContext, InvalidBinding,
    UnsupportedApi, GlFunctionUnavailable, InvalidVersion, BindingChanged, QueryUnwritten,
    NoCallbackObserved, ContextNotCurrent, ApiNotDesktopGl, GlStringFailed,
    GlIntegerFailed, UnsupportedValueQuery, SnapshotCompleted
};
enum class ContextItem : unsigned {
    Api, Context, Display, DrawSurface, ReadSurface, Thread, Version, Vendor, Renderer,
    Glsl, Viewport, Framebuffer, Program, VersionIntegers, Profile, Reserved
};
// Observedは返却値の観測であり、GLエラーなしの保証ではない。
enum class ValueState : std::uint32_t { NotAttempted, Unsupported, Failed, Observed, Truncated };
struct ContextWire {
    std::array<std::uint64_t, 32> words{};
    std::array<ValueState, 16> states{};
    std::array<std::int32_t, 12> integers{};
    std::array<char, 256> version{};
    std::array<char, 128> vendor{}, renderer{};
    std::array<char, 256> glsl{};
    std::array<std::uint64_t, 2> reserved{};
    std::uint64_t& operator[](ContextField f) { return words[static_cast<unsigned>(f)]; }
    std::uint64_t operator[](ContextField f) const { return words[static_cast<unsigned>(f)]; }
    ValueState& state(ContextItem i) { return states[static_cast<unsigned>(i)]; }
    ValueState state(ContextItem i) const { return states[static_cast<unsigned>(i)]; }
};
static_assert(sizeof(ContextWire) == 1152);
constexpr unsigned kContextWords = sizeof(ContextWire) / 8;
struct alignas(4096) ContextPublication {
    std::atomic<std::uint64_t> generation{0};
    std::atomic<std::uint64_t> words[kContextWords]{};
};
static_assert(sizeof(ContextPublication) == 4096);
extern "C" { extern ContextPublication yw1_context_diagnostics; }
ContextWire initialContextWire(unsigned profile);
void publishContext(const ContextWire& wire);
bool validContextWire(const ContextWire& wire);
const char* probeStatusName(std::uint64_t status);
const char* probeReasonName(std::uint64_t reason);
const char* valueStateName(ValueState state);
#if YW1_EGL_GETTER_WHITELIST
std::uint32_t contextValueResult(const ContextWire& wire, ContextItem item);
std::uint16_t contextStringLength(const ContextWire& wire, unsigned index);
#endif
} // 名前空間 yw1::diagnostics
