#pragma once

#include <atomic>

namespace yw1::render {

enum class RenderMode : unsigned { Off, Minimal, Demo };

// 要求は原子的に受け付け、描画threadがcallback境界で一度だけ読み取る。
class RuntimeModeControl {
  public:
    constexpr explicit RuntimeModeControl(RenderMode initial = RenderMode::Demo)
        : requested_(initial) {}

    bool request(RenderMode mode) {
        if (static_cast<unsigned>(mode) > static_cast<unsigned>(RenderMode::Demo)) {
            return false;
        }
        requested_.store(mode, std::memory_order_release);
        return true;
    }

    RenderMode requested() const { return requested_.load(std::memory_order_acquire); }

  private:
    std::atomic<RenderMode> requested_;
};

RuntimeModeControl& runtimeModeControl();

} // 名前空間 yw1::render
