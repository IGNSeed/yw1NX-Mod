#include "arena.hpp"
#include "program/setting.hpp"

extern "C" char __fake_heap[];

namespace {

// 確保元は固定版exlaunchの専用BSSヒープ。使用は単一のModule初期化中に限定する。
std::size_t arenaUsed = 0;

} // 無名名前空間
extern "C" void *malloc(std::size_t size) noexcept {
    return yw1::bootstrap::allocateFromArena(reinterpret_cast<std::byte *>(__fake_heap),
                                             exl::setting::HeapSize, arenaUsed, size);
}

extern "C" void *memcpy(void *destination, const void *source, std::size_t size) noexcept {
    // volatileにより、この実装自身へのmemcpy呼出しへの最適化を防ぐ。
    auto *output = static_cast<volatile unsigned char *>(destination);
    const auto *input = static_cast<const volatile unsigned char *>(source);
    for (std::size_t index = 0; index < size; ++index) {
        output[index] = input[index];
    }
    return destination;
}

extern "C" void *memset(void *destination, int value, std::size_t size) noexcept {
    // IPCページの初期化もゲーム側のlibcやallocatorへ依存させない。
    auto *output = static_cast<volatile unsigned char *>(destination);
    for (std::size_t index = 0; index < size; ++index) {
        output[index] = static_cast<unsigned char>(value);
    }
    return destination;
}

extern "C" int memcmp(const void *left, const void *right, std::size_t size) noexcept {
    // Build ID・symbol照合もゲーム側libcへ依存させない。
    const auto *a = static_cast<const volatile unsigned char *>(left);
    const auto *b = static_cast<const volatile unsigned char *>(right);
    for (std::size_t i = 0; i < size; ++i) {
        if (a[i] != b[i]) {
            return static_cast<int>(a[i]) - static_cast<int>(b[i]);
        }
    }
    return 0;
}
