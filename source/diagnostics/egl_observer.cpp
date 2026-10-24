#include "egl_observer.hpp"

namespace yw1::diagnostics {
EglBoolean SwapObserver::observe(EglHandle display, EglHandle surface, std::uint64_t thread) {
    const auto count = callbacks.fetch_add(1, std::memory_order_relaxed) + 1;
    // 待機せず競合・再入時の診断を省く。元処理はどの呼出しでもちょうど1回だけ実行する。
    const bool owner = !active_.test_and_set(std::memory_order_acquire);
    if (owner && attempts_ < 3) {
        ++attempts_;
        auto next = captureEglSnapshot(api_, thread, count);
        if (attempts_ > 1) {
            contextChanges.fetch_add(next.context != snapshot_.context, std::memory_order_relaxed);
            surfaceChanges.fetch_add(next.surface != snapshot_.surface, std::memory_order_relaxed);
            threadChanges.fetch_add(next.threadId != snapshot_.threadId, std::memory_order_relaxed);
        }
        snapshot_ = next;
    }
    originalCalls.fetch_add(1, std::memory_order_relaxed);
    const auto result = binding_.call(display, surface);
    originalReturns.fetch_add(1, std::memory_order_relaxed);
    if (owner) {
        active_.clear(std::memory_order_release);
    }
    return result;
}
} // 名前空間 yw1::diagnostics
