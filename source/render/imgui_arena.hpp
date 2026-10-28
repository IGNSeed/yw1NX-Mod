#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace yw1::render {
struct ArenaStats {
    std::uint64_t capacity = 0, used = 0, peak = 0, allocations = 0, frees = 0;
    std::uint64_t failures = 0, invalidFrees = 0, live = 0, reserved = 0;
    std::uint64_t attempts = 0, largestAllocation = 0, lastFailedRequest = 0;
};
class ImGuiArena {
  public:
    ImGuiArena() = default;
    ImGuiArena(const ImGuiArena&) = delete;
    ImGuiArena& operator=(const ImGuiArena&) = delete;
    bool initialize(std::span<std::byte> storage);
    void* allocate(std::size_t size);
    bool release(void* pointer);
    ArenaStats stats() const { return stats_; }
    std::size_t largestFree() const;
    bool canReserve(std::size_t bytes) const { return bytes && largestFree() >= bytes; }
    static void* imguiAllocate(std::size_t size, void* owner);
    static void imguiFree(void* pointer, void* owner);
  private:
    struct alignas(std::max_align_t) Block {
        std::size_t span = 0, requested = 0;
        std::uint32_t magic = 0, allocated = 0;
    };
    static constexpr std::uint32_t kMagic = 0x31414D49;
    static constexpr auto kAlign = alignof(std::max_align_t);
    bool valid(std::size_t offset, const Block& block) const;
    Block* block(std::size_t offset) const;
    std::span<std::byte> storage_{};
    ArenaStats stats_{};
};
// 呼出しはRender lease所有threadだけ。並行ImGuiを許可するallocatorではない。
} // 名前空間 yw1::render
