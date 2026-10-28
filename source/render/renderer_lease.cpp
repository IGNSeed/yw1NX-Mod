#include "renderer_lease.hpp"
#include <atomic>
namespace yw1::render {
namespace {
constinit std::atomic_flag lease = ATOMIC_FLAG_INIT;
constinit std::uint64_t serial = 0, owner = 0;
} // 無名名前空間
bool tryEnterRenderer() { return !lease.test_and_set(std::memory_order_acquire); }
void leaveRenderer() { lease.clear(std::memory_order_release); }
bool claimRendererOwner(std::uint64_t& ticket) {
    if (!ticket) {
        if (serial == UINT64_MAX) { return false; }
        ticket = ++serial;
    }
    if (owner && owner != ticket) { return false; }
    owner = ticket; return true;
}
void releaseRendererOwner(std::uint64_t& ticket) {
    if (owner == ticket) { owner = 0; ticket = 0; }
}
} // 名前空間 yw1::render
