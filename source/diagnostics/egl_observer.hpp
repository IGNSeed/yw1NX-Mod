#pragma once
#include "egl_snapshot.hpp"
#include <atomic>
#include <optional>

namespace yw1::diagnostics {

// 検証したOriginalだけで生成する。未接続のHost検証用で、Hook登録・ゲームへのpatchは行わない。
class SwapBinding {
  public:
    static std::optional<SwapBinding> validate(EglSwapBuffers original,
                                               bool (*executable)(std::uintptr_t)) {
        if (!original || !executable || !executable(reinterpret_cast<std::uintptr_t>(original))) {
            return std::nullopt;
        }
        return SwapBinding(original);
    }
    EglBoolean call(EglHandle display, EglHandle surface) const {
        return original_(display, surface);
    }

  private:
    explicit SwapBinding(EglSwapBuffers original) : original_(original) {}
    EglSwapBuffers original_;
};

class SwapObserver {
  public:
    SwapObserver(SwapBinding binding, EglQueryApi api) : binding_(binding), api_(api) {}
    EglBoolean observe(EglHandle display, EglHandle surface, std::uint64_t thread);
    bool readSnapshot(EglSnapshot &output) {
        if (active_.test_and_set(std::memory_order_acquire)) {
            return false;
        }
        output = snapshot_;
        active_.clear(std::memory_order_release);
        return true;
    }
    std::atomic<std::uint64_t> callbacks{0}, originalCalls{0}, originalReturns{0};
    std::atomic<std::uint64_t> contextChanges{0}, surfaceChanges{0}, threadChanges{0};
    unsigned attempts() const { return attempts_.load(std::memory_order_relaxed); }

  private:
    const SwapBinding binding_;
    const EglQueryApi api_;
    std::atomic_flag active_ = ATOMIC_FLAG_INIT;
    EglSnapshot snapshot_{};
    std::atomic<unsigned> attempts_{0};
};

} // 名前空間 yw1::diagnostics
