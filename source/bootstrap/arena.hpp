#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace yw1::bootstrap {

// Bootstrap中の予約ノード専用。確保領域はプロセス終了までモジュールが所有する。
inline void* allocateFromArena(std::byte* base, std::size_t capacity, std::size_t& used,
                               std::size_t size) {
    constexpr std::size_t kAlignment = alignof(std::max_align_t);
    if (base == nullptr || used > capacity || size == 0) {
        return nullptr;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(base + used);
    if (address > std::numeric_limits<std::uintptr_t>::max() - (kAlignment - 1)) {
        return nullptr;
    }
    const auto padding = (kAlignment - address % kAlignment) % kAlignment;
    if (padding > capacity - used || size > capacity - used - padding) {
        return nullptr;
    }
    auto* result = base + used + padding;
    used += padding + size;
    return result;
}

} // 名前空間 yw1::bootstrap
