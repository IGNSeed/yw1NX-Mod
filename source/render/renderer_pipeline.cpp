#include "renderer_pipeline.hpp"
#include "backend_support.hpp"
#include "renderer_lease.hpp"
#include "memory_contract.hpp"
#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include <cmath>

namespace yw1::render {
RenderResult RendererPipeline::skip(RenderResult reason) {
    stats_.disabled = disabled_;
    ++stats_.skips; stats_.last = reason; return reason;
}
bool RendererPipeline::memoryHealthy() {
    const auto s = arena_.stats();
    if (!s.failures && !s.invalidFrees) { return true; }
    stats_.poisoned = true; disabled_ = true;
    return false;
}
void RendererPipeline::cleanupOwned() {
    // 同じ有効Contextとrenderer leaseの内部だけで使用する。
    // 割当失敗後の不完全なCPU構造にはImGuiの解放APIも呼ばない。
    if (!imgui_ || stats_.poisoned) { return; }
    ImGui::SetCurrentContext(imgui_);
    if (backend_) { ImGui_ImplOpenGL3_Shutdown(); backend_ = false; }
    ImGui::DestroyContext(imgui_); imgui_ = nullptr;
    stats_.contextLive = stats_.backendReady = false;
}
bool RendererPipeline::initialize() {
    ++stats_.initAttempts;
    ImGui::SetAllocatorFunctions(ImGuiArena::imguiAllocate, ImGuiArena::imguiFree, &arena_);
    imgui_ = ImGui::CreateContext();
    if (!memoryHealthy() || !imgui_) { return false; }
    ++stats_.contexts;
    stats_.contextLive = true;
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.ConfigFlags = ImGuiConfigFlags_None;
    io.ConfigInputTrickleEventQueue = false;
    backend_ = ImGui_ImplOpenGL3_Init("#version 130");
    stats_.backendReady = backend_;
    if (!memoryHealthy() || !backend_) { return false; }
    // NewFrameの暗黙作成/assertへ進む前に、結果を返せる入口で作成を完了する。
    const bool ready = ImGui_ImplOpenGL3_CreateDeviceObjects();
    if (ready && backendFailure() == BackendFailure::None) { stats_.shader = stats_.buffer = 1; }
    return memoryHealthy() && ready && backendFailure() == BackendFailure::None;
}
RenderResult RendererPipeline::frame(RenderMode mode, const RenderBinding& binding,
    const FrameInput& frameInput, MemoryAdmission admission) {
    if (!monitor_.tryEnter()) { return RenderResult::Context; }
    ScopeExit lease{[&] { monitor_.leave(); }};
    if (disabled_) { return skip(RenderResult::Disabled); }
    if (monitor_.observe(binding, true) != ContextResult::Ready) {
        disabled_ = true; return skip(RenderResult::Context);
    }
    if (mode == RenderMode::Off) {
        lastTime_ = frameInput.timeNs && (!lastTime_ || frameInput.timeNs > lastTime_) ?
            frameInput.timeNs : 0;
        return skip(RenderResult::Disabled);
    }
#if YW1_NATIVE_MINIMAL_ONLY
    if (mode != RenderMode::Minimal) { return skip(RenderResult::Disabled); }
#endif
    // 完全OOM契約と、明示的なNative正常経路実験を分離する。
    const auto s = arena_.stats();
    const bool experimental = admission == MemoryAdmission::NativeExperimental;
    if (admission != MemoryAdmission::IsolatedHostProcess && !experimental) {
        return skip(RenderResult::MemoryUnproven);
    }
    if (!memoryHealthy()) { return skip(RenderResult::AllocationFailure); }
    if (experimental && !normalPathExperimentAdmitted(static_cast<unsigned>(mode), true,
        s.capacity, arena_.largestFree(), imgui_ != nullptr)) {
        return skip(RenderResult::Budget);
    }
    if (experimental) { ++stats_.experimentalAdmissions; }
    if (!arena_.canReserve(256 * 1024)) { disabled_ = true; return skip(RenderResult::Budget); }
    if (!frameInput.width || !frameInput.height || frameInput.width > 4096 ||
        frameInput.height > 4096 || !std::isfinite(frameInput.scaleX) ||
        !std::isfinite(frameInput.scaleY) || frameInput.scaleX <= 0 || frameInput.scaleY <= 0 ||
        frameInput.scaleX > 4 || frameInput.scaleY > 4 || !frameInput.timeNs) {
        return skip(RenderResult::InvalidFrame);
    }
    if (lastTime_ && (frameInput.timeNs <= lastTime_ || frameInput.timeNs - lastTime_ > 1000000000)) {
        lastTime_ = frameInput.timeNs; return skip(RenderResult::InvalidFrame);
    }
    if (!tryEnterRenderer()) { return skip(RenderResult::Context); }
    ScopeExit rendererLease{[] { leaveRenderer(); }};
    if (!claimRendererOwner(ownerTicket_)) { return skip(RenderResult::Context); }
    ScopeExit pendingOwner{[&] { if (!imgui_) { releaseRendererOwner(ownerTicket_); } }};
    bindGl(&api_);
    const auto previous = ImGui::GetCurrentContext();
    ImGuiMemAllocFunc frameAllocate; ImGuiMemFreeFunc frameFree; void* frameAllocatorOwner;
    ImGui::GetAllocatorFunctions(&frameAllocate, &frameFree, &frameAllocatorOwner);
    if (imgui_) {
        ImGui::SetAllocatorFunctions(ImGuiArena::imguiAllocate, ImGuiArena::imguiFree, &arena_);
    }
    ScopeExit restoreImgui{[&] {
        ImGui::SetCurrentContext(previous);
        ImGui::SetAllocatorFunctions(frameAllocate, frameFree, frameAllocatorOwner);
        bindGl(nullptr);
    }};
    clearBackendFailure();
    GlStateScope state(api_);
    if (!state.valid()) {
        ++stats_.stateFailures; stats_.failure = backendFailure();
        disabled_ = true; return skip(RenderResult::State);
    }
    ++stats_.stateSaves;
    bool stateRecorded = false, stateRestored = false;
    const auto restoreState = [&] {
        if (!stateRecorded) {
            stateRecorded = true; stateRestored = state.restore();
            if (stateRestored) { ++stats_.stateRestores; }
            else {
                ++stats_.stateFailures;
                disabled_ = true; stats_.disabled = true;
                if (stats_.failure == BackendFailure::None) { stats_.failure = backendFailure(); }
            }
        }
        return stateRestored;
    };
    ScopeExit restoreEveryExit{[&] { static_cast<void>(restoreState()); }};
    if (!imgui_ && !initialize()) {
        disabled_ = true; stats_.failure = backendFailure();
        ++stats_.initFailures;
        cleanupOwned();
        static_cast<void>(restoreState());
        if (stats_.failure == BackendFailure::VertexShader ||
            stats_.failure == BackendFailure::FragmentShader || stats_.failure == BackendFailure::Program) {
            stats_.shader = 2;
        } else if (stats_.failure == BackendFailure::Buffer) { stats_.buffer = 2; }
        return skip(stats_.poisoned ? RenderResult::AllocationFailure : RenderResult::Backend);
    }
    ImGui::SetCurrentContext(imgui_);
    auto& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(frameInput.width / frameInput.scaleX,
                           frameInput.height / frameInput.scaleY);
    io.DisplayFramebufferScale = ImVec2(frameInput.scaleX, frameInput.scaleY);
    // 最初の時刻だけでは経過時間を測れないため、初期化frameは描画せず次を待つ。
    if (!lastTime_) {
        lastTime_ = frameInput.timeNs;
        return skip(restoreState() ? RenderResult::InvalidFrame : RenderResult::State);
    }
    io.DeltaTime = static_cast<float>(frameInput.timeNs - lastTime_) / 1000000000.0f;
    lastTime_ = frameInput.timeNs;
    ImGui_ImplOpenGL3_NewFrame();
    if (!memoryHealthy()) { return skip(RenderResult::AllocationFailure); }
    ImGui::NewFrame(); ++stats_.newFrames;
    if (!memoryHealthy()) { return skip(RenderResult::AllocationFailure); }
#if !YW1_NATIVE_MINIMAL_ONLY
    if (mode == RenderMode::Demo) { ImGui::ShowDemoWindow(); }
    else
#endif
    {
        ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(240, 70), ImGuiCond_Always);
        ImGui::Begin("YW1 ImGui render check", nullptr,
            ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
        ImGui::TextUnformatted("YW1 OpenGL diagnostics");
        ImGui::End();
    }
    if (!memoryHealthy()) { return skip(RenderResult::AllocationFailure); }
    ImGui::Render();
    ++stats_.renders;
    if (!memoryHealthy()) { return skip(RenderResult::AllocationFailure); }
    const auto before = drawSubmissionCount();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    const auto commands = drawSubmissionCount() - before;
    stats_.drawFrames += commands;
    if (commands) { stats_.font = 1; }
    if (!memoryHealthy()) { return skip(RenderResult::AllocationFailure); }
    stats_.failure = backendFailure();
    const bool restored = restoreState();
    if (!restored || stats_.failure != BackendFailure::None) {
        disabled_ = true;
        if (stats_.failure == BackendFailure::Texture || stats_.failure == BackendFailure::TextureStorage ||
            stats_.failure == BackendFailure::TextureFormat) { stats_.font = 2; }
        if (stats_.failure == BackendFailure::Buffer || stats_.failure == BackendFailure::BufferStorage ||
            stats_.failure == BackendFailure::VertexArray) { stats_.buffer = 2; }
        if (restored) { cleanupOwned(); releaseRendererOwner(ownerTicket_); }
        return skip(RenderResult::Backend);
    }
    // void GL呼出しの送信経路完了。実pixelの表示成功とは区別する。
    if (!commands) { return skip(RenderResult::NoCommands); }
    stats_.last = RenderResult::Submitted;
    return stats_.last;
}
bool RendererPipeline::shutdown(const RenderBinding& current) {
    if (!monitor_.tryEnter()) { return false; }
    ScopeExit lease{[&] { monitor_.leave(); }};
    if (!imgui_ && disabled_ && !stats_.poisoned && !ownerTicket_ && !monitor_.changes() &&
        current.context && current == monitor_.owner()) { return true; }
    if (monitor_.changes() || current != monitor_.owner() || !current.context || !imgui_ ||
        stats_.poisoned) {
        // 同じ数値のhandleが再利用されても旧Contextと断定しない。
        // 喪失後の資源はprocess終了まで保留し、別Contextでdeleteしない。
        return false;
    }
    if (!tryEnterRenderer()) { return false; }
    ScopeExit rendererLease{[] { leaveRenderer(); }};
    bindGl(&api_);
    const auto previous = ImGui::GetCurrentContext();
    ImGuiMemAllocFunc frameAllocate; ImGuiMemFreeFunc frameFree; void* frameAllocatorOwner;
    ImGui::GetAllocatorFunctions(&frameAllocate, &frameFree, &frameAllocatorOwner);
    ImGui::SetAllocatorFunctions(ImGuiArena::imguiAllocate, ImGuiArena::imguiFree, &arena_);
    ImGui::SetCurrentContext(imgui_);
    GlStateScope state(api_);
    if (!state.valid()) {
        ImGui::SetCurrentContext(previous);
        ImGui::SetAllocatorFunctions(frameAllocate, frameFree, frameAllocatorOwner);
        bindGl(nullptr); return false;
    }
    if (backend_) { ImGui_ImplOpenGL3_Shutdown(); backend_ = false; }
    auto* released = imgui_;
    ImGui::DestroyContext(imgui_); imgui_ = nullptr;
    stats_.contextLive = stats_.backendReady = false;
    ImGui::SetAllocatorFunctions(frameAllocate, frameFree, frameAllocatorOwner);
    ImGui::SetCurrentContext(previous == released ? nullptr : previous);
    const bool restored = state.restore();
    bindGl(nullptr); disabled_ = true; stats_.disabled = true;
    if (restored) { releaseRendererOwner(ownerTicket_); }
    return restored;
}
} // 名前空間 yw1::render
