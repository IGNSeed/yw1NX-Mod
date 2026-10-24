#pragma once
#include "egl_hook_guard.hpp"
#include "module_wire.hpp"
#include <atomic>
#if YW1_BOOTSTRAP_SAFE_HOOK_DIAGNOSTIC
#include "hooking/safe_hook_installer.hpp"
#endif

namespace yw1::diagnostics {
constexpr std::uint64_t kEglMagic = 0x324C4745315759;
#if YW1_BOOTSTRAP_SAFE_HOOK_DIAGNOSTIC
constexpr std::uint32_t kEglWireAbi = 2;
#else
constexpr std::uint32_t kEglWireAbi = 1;
#endif
// 各bitは照会の発行を表す。GLエラーなしの成功を表すものではない。
enum EglQueryFlags : std::uint32_t {
    kStringsIssued = 1,
    kViewportIssued = 2,
    kFramebufferIssued = 4,
    kProgramIssued = 8,
    kGlslIssued = 16,
    kVersionIssued = 32,
    kProfileIssued = 64
};
struct alignas(64) EglDiagnosticWire {
    std::uint64_t magic = kEglMagic;
    std::uint32_t abi = kEglWireAbi, size = sizeof(EglDiagnosticWire);
    std::uint64_t buildTag = kBuildTag;
    std::uint32_t installAttempted = 0, installResult = UINT32_MAX;
    std::uint32_t guardStatus = static_cast<std::uint32_t>(HookGuardStatus::NotAttempted);
    std::uint32_t blockedReason = 0;
    std::uint64_t callbackCount = 0, originalCalls = 0, originalReturns = 0;
    std::uint64_t thread = 0, swaps = 0, contextChanges = 0, surfaceChanges = 0;
    std::uint64_t context = 0, display = 0, surface = 0;
    std::uint32_t api = 0, bindingKnown = 0, snapshotStatus = UINT32_MAX, queryFlags = 0;
    char version[256]{}, vendor[128]{}, renderer[128]{}, glsl[256]{};
    std::int32_t viewport[4]{}, framebuffer = 0, program = 0, major = 0, minor = 0, profile = 0;
#if YW1_BOOTSTRAP_SAFE_HOOK_DIAGNOSTIC
    hooking::InstallerReport installer{};
#endif
};
#if YW1_BOOTSTRAP_SAFE_HOOK_DIAGNOSTIC
static_assert(sizeof(EglDiagnosticWire) == 1024);
#else
static_assert(sizeof(EglDiagnosticWire) == 960);
#endif
static_assert(alignof(EglDiagnosticWire) == 64);
static_assert(sizeof(EglDiagnosticWire) % 8 == 0);

inline bool validEglWire(const EglDiagnosticWire& wire) {
    const auto terminated = [](const auto& text) {
        for (char value : text) {
            if (value == 0) {
                return true;
            }
        }
        return false;
    };
    const bool installerValid =
#if YW1_BOOTSTRAP_SAFE_HOOK_DIAGNOSTIC
        static_cast<std::uint32_t>(wire.installer.state) <= 6 &&
        static_cast<std::uint32_t>(wire.installer.backend) <= 1 &&
        wire.installer.quiescenceVerified <= 1 && wire.installer.commitAttempted <= 1 &&
        wire.installer.rollbackAttempted <= 1 && wire.installer.active <= 1 &&
        wire.installer.resourcesRetained <= 1 &&
        (wire.installer.active == 0 ||
         (wire.installer.state == hooking::HookState::Active &&
          wire.installer.commitAttempted == 1 && wire.installer.commit == 0 &&
          wire.installer.publish == 0 && wire.installer.trampoline == 0 &&
          wire.installer.quiescenceVerified == 1)) &&
        (wire.installer.state != hooking::HookState::Active || wire.installer.active == 1);
#else
        true;
#endif
    return installerValid && wire.magic == kEglMagic && wire.abi == kEglWireAbi &&
           wire.size == sizeof(wire) && wire.buildTag == kBuildTag && wire.installAttempted <= 1 &&
           wire.bindingKnown <= 1 && (wire.queryFlags & ~127u) == 0 &&
           (wire.snapshotStatus <= 7 || wire.snapshotStatus == UINT32_MAX) &&
           (wire.guardStatus <= 4 || wire.guardStatus == UINT32_MAX) &&
           wire.originalReturns <= wire.originalCalls && wire.originalCalls <= wire.callbackCount &&
           terminated(wire.version) && terminated(wire.vendor) && terminated(wire.renderer) &&
           terminated(wire.glsl);
}

struct alignas(4096) EglPublication {
    std::atomic<std::uint64_t> generation{0};
    std::atomic<std::uint64_t> words[sizeof(EglDiagnosticWire) / 8]{};
};
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
extern "C" {
extern EglPublication yw1_egl_diagnostics;
}
void publishEglDiagnostic(const EglDiagnosticWire& value);
} // 名前空間 yw1::diagnostics
