#include "render_diagnostic.hpp"
#include "diagnostics/render_wire.hpp"
#include "diagnostics/module_wire.hpp"
#include "render/context_monitor.hpp"
#include "render/imgui_arena.hpp"
#include "render/gl_function_specs.hpp"
#include "render/gl_api.hpp"
#include "render/scope_exit.hpp"
#include <common.hpp>
#if YW1_NATIVE_RENDER_EXPERIMENT
#include "render/renderer_pipeline.hpp"
#include "render/frame_clock.hpp"
#include <new>
#endif

namespace yw1::bootstrap {
namespace {
using namespace diagnostics;
using namespace render;
using F = RenderField;
alignas(std::max_align_t) constinit std::array<std::byte, YW1_IMGUI_ARENA_BYTES> arenaStorage{};
constinit ImGuiArena arena;
constinit ContextMonitor monitor;
constinit GlApi verifiedApi{};
constinit RenderWire wire = initialRenderWire(3, YW1_RENDER_MODE);
constinit std::atomic<std::uint64_t> busySkips{0};
ContextPlatform* contextPlatform = nullptr;
ContextApi api{};
game::ModuleImage graphics{};
std::uint64_t textBase = 0, textSize = 0;
bool configured = false, resolverAttempted = false;
#if YW1_NATIVE_RENDER_EXPERIMENT
alignas(RendererPipeline) constinit std::array<std::byte, sizeof(RendererPipeline)> pipelineStorage{};
RendererPipeline* pipeline = nullptr;
#endif
bool readable(std::uint64_t address, std::size_t size, unsigned permission) {
    if (!size || address > UINTPTR_MAX - size) { return false; }
    MemoryInfo info{}; u32 page = 0;
    return svcQueryMemory(&info, &page, address) == 0 && info.attr == 0 &&
        (info.perm & permission) == permission && contains(info.addr, info.size, address, size);
}
class NativeGlMemory final : public GlMemory {
  public:
    bool read(std::uint64_t address, std::span<std::byte> bytes, bool code) override {
        if (!contains(graphics.base, graphics.size, address, bytes.size()) ||
            (code && !contains(textBase, textSize, address, bytes.size())) ||
            !readable(address, bytes.size(), code ? Perm_Rx : Perm_R)) { return false; }
        for (unsigned i = 0; i < bytes.size(); ++i) {
            bytes[i] = static_cast<std::byte>(__atomic_load_n(
                reinterpret_cast<const std::uint8_t*>(address + i), __ATOMIC_ACQUIRE));
        }
        return true;
    }
    GlProc lookup(const char* name) override { return reinterpret_cast<GlProc>(api.getProc(name)); }
};
void finish(RenderDisable reason) {
    // Nativeでは一度登録した表を同一Contextだけで使用する。
    // 早期skipで表を空にすると、次callbackのqueryがNULL呼出しになってしまう。
#if !YW1_NATIVE_RENDER_EXPERIMENT
    if (reason != RenderDisable::OomUnproven && reason != RenderDisable::Thread &&
        reason != RenderDisable::DiagnosticOnly) { verifiedApi = {}; }
#endif
    wire[F::DisableReason] = static_cast<unsigned>(reason);
    wire[F::OwnerContext] = monitor.owner().context; wire[F::OwnerThread] = monitor.owner().thread;
    wire[F::Busy] = busySkips.load(std::memory_order_relaxed);
    const auto stats = arena.stats();
    wire[F::ArenaCapacity] = stats.capacity; wire[F::ArenaUsed] = stats.used;
    wire[F::ArenaPeak] = stats.peak; wire[F::ArenaAllocations] = stats.allocations;
    wire[F::ArenaFrees] = stats.frees; wire[F::ArenaFailures] = stats.failures;
    wire[F::ArenaInvalidFrees] = stats.invalidFrees;
#if YW1_RENDER_MEMORY_DIAGNOSTICS
    wire[F::ArenaAttempts] = stats.attempts;
    wire[F::ArenaLargestAllocation] = stats.largestAllocation;
    wire[F::ArenaLastFailedRequest] = stats.lastFailedRequest;
    wire[F::ArenaLargestFree] = arena.largestFree();
    wire[F::ArenaReserved] = stats.reserved;
    wire[F::NativeMemoryAdmitted] = nativeMemoryAdmitted(YW1_RENDER_MODE);
#endif
#if YW1_NATIVE_RENDER_EXPERIMENT
    wire[F::ArenaLive] = stats.live; wire[F::AllocatorType] = 1;
    if (pipeline) {
        const auto& p = pipeline->stats();
        wire[F::InitAttempts] = p.initAttempts; wire[F::ImGuiContexts] = p.contexts;
        wire[F::NewFrames] = p.newFrames; wire[F::RenderCalls] = p.renders;
        wire[F::DrawSubmitted] = p.drawFrames; wire[F::Shader] = p.shader;
        wire[F::Font] = p.font; wire[F::Buffer] = p.buffer;
        wire[F::StateSaves] = p.stateSaves; wire[F::StateRestores] = p.stateRestores;
        wire[F::StateFailures] = p.stateFailures; wire[F::BackendFailure] = static_cast<unsigned>(p.failure);
        wire[F::BackendInitialized] = p.backendReady; wire[F::ContextLive] = p.contextLive;
        wire[F::LastRenderResult] = static_cast<unsigned>(p.last);
        wire[F::NormalPathAdmission] = p.experimentalAdmissions != 0;
        wire[F::InitFailures] = p.initFailures;
        wire[F::RendererDisabled] = p.disabled || monitor.changes() != 0;
    }
#endif
    publishRender(wire);
}
} // 無名名前空間
void prepareRenderDiagnostic(ContextPlatform& platform, const ContextApi& value,
    const game::ModuleImage& module, std::uint64_t rxBase, std::uint64_t rxSize) {
    contextPlatform = &platform; api = value; graphics = module;
    textBase = rxBase; textSize = rxSize; configured = true;
}
#if YW1_NATIVE_RENDER_EXPERIMENT
void probeRenderCallback(std::uintptr_t swapDisplay, std::uintptr_t swapSurface) {
#else
void probeRenderCallback() {
#endif
    if (!monitor.tryEnter()) { busySkips.fetch_add(1, std::memory_order_relaxed); return; }
    ScopeExit lease{[&] { monitor.leave(); }};
#if YW1_DEVELOPMENT_MODULE
    const auto requestedMode = runtimeModeControl().requested();
    wire[F::Mode] = static_cast<unsigned>(requestedMode);
    wire[F::NativeMemoryBlockMask] = nativeMemoryBlockMask(static_cast<unsigned>(requestedMode));
#endif
    ++wire[F::Callbacks]; ++wire[F::Skips];
    if (!configured || !contextPlatform || contextPlatform->validate(api) != 0 ||
        contextPlatform->ready() != ProbeReason::None) {
        RenderBinding missing{}; missing.thread = monitor.owner().thread;
        static_cast<void>(monitor.observe(missing, false));
        wire[F::ContextChanges] = monitor.changes();
        finish(RenderDisable::Identity); return;
    }
    ++wire[F::ContextChecks];
    std::uint64_t thread = 0;
    const auto threadResult = contextPlatform->threadId(thread);
    wire[F::ThreadResult] = threadResult;
    if (threadResult != 0) { finish(RenderDisable::Thread); return; }
    RenderBinding binding{reinterpret_cast<std::uintptr_t>(api.context()),
        reinterpret_cast<std::uintptr_t>(api.display()),
        reinterpret_cast<std::uintptr_t>(api.surface(0x3059)),
        reinterpret_cast<std::uintptr_t>(api.surface(0x305A)), thread, api.queryApi()};
    wire[F::Context] = binding.context; wire[F::Display] = binding.display;
    wire[F::DrawSurface] = binding.draw; wire[F::ReadSurface] = binding.read;
    wire[F::Thread] = thread; wire[F::Api] = binding.api;
    const auto result = monitor.observe(binding, true);
    wire[F::BindingResult] = static_cast<unsigned>(result);
    wire[F::ContextChanges] = monitor.changes();
    wire[F::Stage] = static_cast<unsigned>(RenderStage::Context);
    if (result != ContextResult::Ready) {
        finish(result == ContextResult::DifferentThread ? RenderDisable::Thread :
            result == ContextResult::Lost ? RenderDisable::Lost :
            result == ContextResult::Changed || result == ContextResult::Disabled ?
                RenderDisable::Changed : RenderDisable::Context);
        return;
    }
#if YW1_DEVELOPMENT_MODULE
    if (requestedMode == RenderMode::Off) {
        wire[F::Stage] = static_cast<unsigned>(RenderStage::Disabled);
        if (pipeline) {
            static_cast<void>(pipeline->frame(RenderMode::Off, binding, {},
                MemoryAdmission::NativeExperimental));
        } else {
            wire[F::LastRenderResult] = static_cast<unsigned>(RenderResult::Disabled);
        }
        finish(RenderDisable::None);
        return;
    }
#endif
    if (!wire[F::ArenaInitialized]) {
        wire[F::Stage] = static_cast<unsigned>(RenderStage::Allocator);
        if (!readable(reinterpret_cast<std::uintptr_t>(arenaStorage.data()), arenaStorage.size(),
                      Perm_Rw) || !arena.initialize(arenaStorage)) {
            finish(RenderDisable::Arena); return;
        }
        wire[F::ArenaInitialized] = 1;
    }
    if (!resolverAttempted) {
        wire[F::Stage] = static_cast<unsigned>(RenderStage::Resolver);
        resolverAttempted = true; ++wire[F::LoaderAttempts];
        NativeGlMemory memory;
        GlResolver resolver;
        std::array<GlProc, std::size(kGlFunctions)> functions{};
        const auto stats = resolver.resolve(graphics, kGlFunctions, memory, functions);
        wire[F::GlAttempted] = stats.attempted; wire[F::GlVerified] = stats.verified;
        wire[F::GlRejected] = stats.rejected; wire[F::GlFirstFailure] = stats.firstFailure;
        wire[F::GlResult] = static_cast<unsigned>(stats.result);
        wire[F::GlVerifiedMask] = stats.verifiedMask; wire[F::GlRejectedMask] = stats.rejectedMask;
        if (stats.result == ResolveResult::Ready && !verifiedApi.assign(stats, functions)) {
            wire[F::GlResult] = static_cast<unsigned>(ResolveResult::Missing);
        }
        // 完全OOM契約は別に記録し、明示した実験Profileだけで通常描画へ進む。
    }
    if (wire[F::GlVerified] != wire[F::GlRequired] || wire[F::GlResult] != 0) {
        finish(RenderDisable::Resolver); return;
    }
    wire[F::Stage] = static_cast<unsigned>(RenderStage::Admission);
#if YW1_NATIVE_RENDER_EXPERIMENT
    wire[F::SwapDisplay] = swapDisplay; wire[F::SwapSurface] = swapSurface;
    if (!swapDisplay || !swapSurface || swapDisplay != binding.display || swapSurface != binding.draw) {
        ++wire[F::BindingMismatch]; finish(RenderDisable::SwapBinding); return;
    }
    std::uintptr_t stack = 0; asm volatile("mov %0, sp" : "=r"(stack));
    MemoryInfo stackInfo{}; u32 page = 0;
    if (svcQueryMemory(&stackInfo, &page, stack) != 0 || stackInfo.perm != Perm_Rw ||
        stackInfo.attr != 0 || !contains(stackInfo.addr, stackInfo.size, stack, 1) ||
        stack - stackInfo.addr < 65536) {
        finish(RenderDisable::Stack); return;
    }
    wire[F::StackAvailable] = stack - stackInfo.addr;
    GLint viewport[4]{-1,-1,-1,-1}, framebuffer = -1;
    verifiedApi.GetIntegerv(GL_VIEWPORT, viewport);
    verifiedApi.GetIntegerv(0x8CA6, &framebuffer);
    if (framebuffer != 0 || viewport[0] != 0 || viewport[1] != 0 ||
        viewport[2] <= 0 || viewport[3] <= 0 || viewport[2] > 4096 || viewport[3] > 4096) {
        finish(RenderDisable::FrameInput); return;
    }
    std::uint64_t frequency = 0;
    asm volatile("mrs %0, cntfrq_el0" : "=r"(frequency));
    const auto time = ticksToNanoseconds(svcGetSystemTick(), frequency);
    wire[F::ClockFrequency] = frequency; wire[F::LastFrameTimeNs] = time;
    wire[F::ViewportWidth] = viewport[2]; wire[F::ViewportHeight] = viewport[3];
    if (!time) { finish(RenderDisable::FrameInput); return; }
    if (!pipeline) { pipeline = ::new (pipelineStorage.data()) RendererPipeline(arena, verifiedApi); }
    wire[F::Stage] = static_cast<unsigned>(RenderStage::Rendering);
    const FrameInput input{static_cast<unsigned>(viewport[2]), static_cast<unsigned>(viewport[3]),1,1,time};
    const auto renderResult = pipeline->frame(
#if YW1_DEVELOPMENT_MODULE
        requestedMode,
#else
        static_cast<RenderMode>(YW1_RENDER_MODE),
#endif
        binding, input,
                                        MemoryAdmission::NativeExperimental);
    if (renderResult == RenderResult::Submitted) { --wire[F::Skips]; }
    finish(renderResult == RenderResult::Submitted ? RenderDisable::None :
        renderResult == RenderResult::InvalidFrame ? RenderDisable::FrameInput : RenderDisable::Pipeline);
    return;
#elif YW1_RENDER_ACTIVATION_DIAGNOSTICS
    wire[F::AllocatorType] = 1;
    finish(RenderDisable::DiagnosticOnly);
    return;
#endif
    wire[F::Stage] = static_cast<unsigned>(RenderStage::Admission);
    finish(RenderDisable::OomUnproven);
}
} // 名前空間 yw1::bootstrap
