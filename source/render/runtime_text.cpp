#include "runtime_text.hpp"
#include <cstdint>
namespace yw1::render::runtime {
std::size_t length(const char* text) {
    const volatile auto* p = text;
    std::size_t n = 0;
    while (p[n]) {
        ++n;
    }
    return n;
}
int compare(const char* a, const char* b, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        const auto x = static_cast<unsigned char>(a[i]), y = static_cast<unsigned char>(b[i]);
        if (x != y || !x) {
            return static_cast<int>(x) - y;
        }
    }
    return 0;
}
const char* findChar(const char* text, int value) {
    for (;; ++text) {
        if (static_cast<unsigned char>(*text) == static_cast<unsigned char>(value)) {
            return text;
        }
        if (!*text) {
            return nullptr;
        }
    }
}
const char* findText(const char* text, const char* wanted) {
    if (!*wanted) {
        return text;
    }
    for (; *text; ++text) {
        std::size_t n = 0;
        while (wanted[n] && text[n] && wanted[n] == text[n]) {
            ++n;
        }
        if (!wanted[n]) {
            return text;
        }
    }
    return nullptr;
}
void* move(void* to, const void* from, std::size_t count) {
    auto* out = static_cast<volatile unsigned char*>(to);
    const auto* in = static_cast<const volatile unsigned char*>(from);
    if (reinterpret_cast<std::uintptr_t>(to) < reinterpret_cast<std::uintptr_t>(from)) {
        for (std::size_t i = 0; i < count; ++i) {
            out[i] = in[i];
        }
    } else {
        for (std::size_t i = count; i > 0; --i) {
            out[i - 1] = in[i - 1];
        }
    }
    return to;
}
void sort(void* data, std::size_t count, std::size_t width, int (*cmp)(const void*, const void*)) {
    if (!data || !cmp || !width || count > SIZE_MAX / width || count < 2) {
        return;
    }
    auto* bytes = static_cast<unsigned char*>(data);
    const auto swap = [&](std::size_t a, std::size_t b) {
        for (std::size_t i = 0; i < width; ++i) {
            const auto tmp = bytes[a * width + i];
            bytes[a * width + i] = bytes[b * width + i];
            bytes[b * width + i] = tmp;
        }
    };
    const auto sift = [&](std::size_t root, std::size_t end) {
        while (root < end / 2) {
            auto child = root * 2 + 1;
            if (child + 1 < end && cmp(bytes + child * width, bytes + (child + 1) * width) < 0) {
                ++child;
            }
            if (cmp(bytes + root * width, bytes + child * width) >= 0) {
                break;
            }
            swap(root, child);
            root = child;
        }
    };
    // 明示stackもheapも使用しないheapsort。qsortと同じく安定順は保証しない。
    for (std::size_t i = count / 2; i > 0; --i) {
        sift(i - 1, count);
    }
    for (std::size_t end = count - 1; end > 0; --end) {
        swap(0, end);
        sift(0, end);
    }
}
}
