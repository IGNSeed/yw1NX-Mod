#pragma once
#include "context_monitor.hpp"
#include "gl_state.hpp"
#include "imgui_arena.hpp"
#include "runtime_mode.hpp"
#include "imgui.h"
struct ImGuiContext;

namespace yw1::render {
enum class MemoryAdmission : unsigned { Unproven, IsolatedHostProcess, NativeExperimental };
enum class RenderResult : unsigned {
    Submitted, Disabled, Context, MemoryUnproven, Budget, InvalidFrame, Backend, State, NoCommands,
    AllocationFailure
};
struct FrameInput {
    unsigned width = 0, height = 0;
    float scaleX = 0, scaleY = 0;
    std::uint64_t timeNs = 0;
};
struct PipelineStats {
    std::uint64_t initAttempts = 0, contexts = 0, newFrames = 0, drawFrames = 0, skips = 0;
    std::uint64_t stateSaves = 0, stateRestores = 0, stateFailures = 0;
    RenderResult last = RenderResult::Disabled;
    BackendFailure failure = BackendFailure::None;
    std::uint64_t renders = 0, initFailures = 0, experimentalAdmissions = 0;
    unsigned shader = 0, font = 0, buffer = 0;
    bool contextLive = false, backendReady = false, poisoned = false, disabled = false;
};
class RendererPipeline {
  public:
    RendererPipeline(ImGuiArena& arena, GlApi& api) : arena_(arena), api_(api) {}
    RenderResult frame(RenderMode mode, const RenderBinding& binding, const FrameInput& frame,
                       MemoryAdmission admission);
    bool shutdown(const RenderBinding& current);
    const PipelineStats& stats() const { return stats_; }
  private:
    bool initialize();
    bool memoryHealthy();
    void cleanupOwned();
    RenderResult skip(RenderResult reason);
    ImGuiArena& arena_;
    GlApi& api_;
    ContextMonitor monitor_;
    ImGuiContext* imgui_ = nullptr;
    PipelineStats stats_{};
    std::uint64_t lastTime_ = 0;
    std::uint64_t ownerTicket_ = 0;
    bool backend_ = false, disabled_ = false;
};
} // 名前空間 yw1::render
