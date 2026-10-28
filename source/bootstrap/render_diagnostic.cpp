#include "render_diagnostic.hpp"
#include "diagnostics/render_wire.hpp"
#include "diagnostics/module_wire.hpp"
#include "render/context_monitor.hpp"
#include "render/imgui_arena.hpp"
#include "render/gl_function_specs.hpp"
#include "render/gl_api.hpp"
#include "render/scope_exit.hpp"
#include <common.hpp>

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
    if (reason != RenderDisable::OomUnproven && reason != RenderDisable::Thread) { verifiedApi = {}; }
    wire[F::DisableReason] = static_cast<unsigned>(reason);
    wire[F::OwnerContext] = monitor.owner().context; wire[F::OwnerThread] = monitor.owner().thread;
    wire[F::Busy] = busySkips.load(std::memory_order_relaxed);
    const auto stats = arena.stats();
    wire[F::ArenaCapacity] = stats.capacity; wire[F::ArenaUsed] = stats.used;
    wire[F::ArenaPeak] = stats.peak; wire[F::ArenaAllocations] = stats.allocations;
    wire[F::ArenaFrees] = stats.frees; wire[F::ArenaFailures] = stats.failures;
    wire[F::ArenaInvalidFrees] = stats.invalidFrees;
    publishRender(wire);
}
} // 無名名前空間
void prepareRenderDiagnostic(ContextPlatform& platform, const ContextApi& value,
    const game::ModuleImage& module, std::uint64_t rxBase, std::uint64_t rxSize) {
    contextPlatform = &platform; api = value; graphics = module;
    textBase = rxBase; textSize = rxSize; configured = true;
}
void probeRenderCallback() {
    if (!monitor.tryEnter()) { busySkips.fetch_add(1, std::memory_order_relaxed); return; }
    ScopeExit lease{[&] { monitor.leave(); }};
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
        // 型付き登録後も、未成立のOOM契約をpointer表で迂回して描画しない。
    }
    if (wire[F::GlVerified] != wire[F::GlRequired] || wire[F::GlResult] != 0) {
        finish(RenderDisable::Resolver); return;
    }
    wire[F::Stage] = static_cast<unsigned>(RenderStage::Admission);
    finish(RenderDisable::OomUnproven);
}
} // 名前空間 yw1::bootstrap
