#include "context_monitor.hpp"

namespace yw1::render {
ContextResult ContextMonitor::observe(const RenderBinding& binding, bool validated) {
    if (disabled_) { return ContextResult::Disabled; }
    if (owner_.thread && binding.thread != owner_.thread) {
        return ContextResult::DifferentThread;
    }
    const bool current = validated && binding.thread && binding.api == 0x30A2 &&
        binding.context && binding.display && binding.draw && binding.read;
    if (!current) {
        if (owner_.context) { ++changes_; disabled_ = true; return ContextResult::Lost; }
        return ContextResult::Unavailable;
    }
    if (!owner_.context) { owner_ = binding; return ContextResult::Ready; }
    if (binding != owner_) {
        ++changes_; disabled_ = true;
        // 別Contextで旧資源のdeleteやpointer再利用を行わない。process寿命まで保留する。
        return ContextResult::Changed;
    }
    return ContextResult::Ready;
}
} // 名前空間 yw1::render
