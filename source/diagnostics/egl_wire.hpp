#pragma once
#include "egl_hook_guard.hpp"
#include "module_wire.hpp"
#include <atomic>

namespace yw1::diagnostics {
constexpr std::uint64_t kEglMagic = 0x324C4745315759;
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
    std::uint32_t abi = 1, size = sizeof(EglDiagnosticWire);
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
};
static_assert(sizeof(EglDiagnosticWire) == 960);
static_assert(alignof(EglDiagnosticWire) == 64);
static_assert(sizeof(EglDiagnosticWire) % 8 == 0);

inline bool validEglWire(const EglDiagnosticWire &wire) {
    const auto terminated = [](const auto &text) {
        for (char value : text) {
            if (value == 0) {
                return true;
            }
        }
        return false;
    };
    return wire.magic == kEglMagic && wire.abi == 1 && wire.size == sizeof(wire) &&
           wire.buildTag == kBuildTag && wire.installAttempted <= 1 && wire.bindingKnown <= 1 &&
           (wire.queryFlags & ~127u) == 0 &&
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
void publishEglDiagnostic(const EglDiagnosticWire &value);
} // 名前空間 yw1::diagnostics
