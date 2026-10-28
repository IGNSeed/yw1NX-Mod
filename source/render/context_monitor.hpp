#pragma once

#include <atomic>
#include <cstdint>

namespace yw1::render {
struct RenderBinding {
    std::uint64_t context = 0, display = 0, draw = 0, read = 0, thread = 0;
    std::uint32_t api = 0;
    bool operator==(const RenderBinding&) const = default;
};
enum class ContextResult : std::uint32_t {
    Ready, Busy, Unavailable, DifferentThread, Changed, Lost, Disabled
};
class ContextMonitor {
  public:
    bool tryEnter() { return !busy_.test_and_set(std::memory_order_acquire); }
    void leave() { busy_.clear(std::memory_order_release); }
    ContextResult observe(const RenderBinding& binding, bool validated);
    void disable() { disabled_ = true; }
    const RenderBinding& owner() const { return owner_; }
    std::uint64_t changes() const { return changes_; }
    // observeと所有資源の操作は同じleaseの内部だけで行う。
  private:
    std::atomic_flag busy_ = ATOMIC_FLAG_INIT;
    RenderBinding owner_{};
    bool disabled_ = false;
    std::uint64_t changes_ = 0;
};
} // 名前空間 yw1::render
