#include "logging/memory_log.hpp"
#include "render/runtime_text.hpp"
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstring>
#define STB_SPRINTF_STATIC
#define STB_SPRINTF_IMPLEMENTATION
#define STB_SPRINTF_NOUNALIGNED
#include YW1_STB_SPRINTF_HEADER

namespace {
// 全てModule私有。SDKのerrno、終了handler、ゲームheapへ登録しない。
constinit int localErrno = 0;
constinit void (*exitEntries[32])(){};
constinit unsigned exitCount = 0, printCount = 0;
}
#if !YW1_RUNTIME_HOST_TEST
extern "C" int* __errno() {
    return &localErrno;
}
extern "C" void* memmove(void* to, const void* from, std::size_t size) noexcept {
    auto* out = static_cast<volatile unsigned char*>(to);
    const auto* in = static_cast<const volatile unsigned char*>(from);
    if (reinterpret_cast<std::uintptr_t>(to) < reinterpret_cast<std::uintptr_t>(from)) {
        for (std::size_t i = 0; i < size; ++i) {
            out[i] = in[i];
        }
    } else {
        for (std::size_t i = size; i > 0; --i) {
            out[i - 1] = in[i - 1];
        }
    }
    return to;
}
extern "C" int atexit(void (*function)()) noexcept {
    if (!function || exitCount >= 32) {
        return -1;
    }
    exitEntries[exitCount++] = function;
    // Module unloadを行わず、static所有物はprocess寿命まで保持する。
    return 0;
}
extern "C" std::size_t strlen(const char* s) noexcept {
    return yw1::render::runtime::length(s);
}
extern "C" int strcmp(const char* a, const char* b) noexcept {
    return yw1::render::runtime::compare(a, b, SIZE_MAX);
}
extern "C" int strncmp(const char* a, const char* b, std::size_t n) noexcept {
    return yw1::render::runtime::compare(a, b, n);
}
extern "C" char* strchr(const char* s, int c) noexcept {
    return const_cast<char*>(yw1::render::runtime::findChar(s, c));
}
extern "C" char* strstr(const char* s, const char* w) noexcept {
    return const_cast<char*>(yw1::render::runtime::findText(s, w));
}
extern "C" char* strncpy(char* out, const char* from, std::size_t count) noexcept {
    std::size_t i = 0;
    for (; i < count && from[i]; ++i) {
        out[i] = from[i];
    }
    for (; i < count; ++i) {
        out[i] = 0;
    }
    return out;
}
extern "C" char* stpcpy(char* out, const char* from) noexcept {
    while (*from) {
        *out++ = *from++;
    }
    *out = 0;
    return out;
}
extern "C" char* strcpy(char* out, const char* from) noexcept {
    stpcpy(out, from);
    return out;
}
extern "C" void* memchr(const void* data, int c, std::size_t count) noexcept {
    const auto* p = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < count; ++i) {
        if (p[i] == static_cast<unsigned char>(c)) {
            return const_cast<unsigned char*>(p + i);
        }
    }
    return nullptr;
}
extern "C" int toupper(int c) noexcept {
    return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c;
}
extern "C" void qsort(void* data, std::size_t count, std::size_t width,
                      int (*cmp)(const void*, const void*)) noexcept {
    yw1::render::runtime::sort(data, count, width, cmp);
}
#endif
extern "C" int __wrap_vsnprintf(char* out, std::size_t size, const char* fmt, va_list args) {
    if (!fmt || size > INT_MAX || (!out && size)) {
        return -1;
    }
    return stbsp_vsnprintf(out, static_cast<int>(size), fmt, args);
}
extern "C" int __wrap_snprintf(char* out, std::size_t size, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    const auto count = __wrap_vsnprintf(out, size, fmt, args);
    va_end(args);
    return count;
}
extern "C" int __wrap_printf(const char* fmt, ...) {
    char out[512];
    va_list args;
    va_start(args, fmt);
    const auto count = __wrap_vsnprintf(out, sizeof(out), fmt, args);
    va_end(args);
    if (count > 0 && printCount++ < 8) {
        const auto length = static_cast<std::size_t>(count) < sizeof(out) ? count : sizeof(out) - 1;
        static_cast<void>(yw1::logging::memoryLog().append(out, length));
    }
    return count;
}
extern "C" double __wrap_atof(const char* text) {
    if (!text) {
        return 0;
    }
    while (*text == ' ' || *text == '\t') {
        ++text;
    }
    const bool negative = *text == '-';
    if (*text == '-' || *text == '+') {
        ++text;
    }
    double value = 0, scale = 1;
    unsigned digits = 0;
    while (*text >= '0' && *text <= '9' && digits++ < 64) {
        value = value * 10 + (*text++ - '0');
    }
    if (*text == '.') {
        ++text;
        while (*text >= '0' && *text <= '9' && digits++ < 64) {
            scale *= 0.1;
            value += (*text++ - '0') * scale;
        }
    }
    if (*text == 'e' || *text == 'E') {
        ++text;
        const bool minus = *text == '-';
        if (*text == '-' || *text == '+') {
            ++text;
        }
        unsigned exponent = 0, count = 0;
        while (*text >= '0' && *text <= '9' && count++ < 3) {
            exponent = exponent * 10 + (*text++ - '0');
        }
        if (exponent > 308) {
            return minus || value == 0 ? 0 : (negative ? -HUGE_VAL : HUGE_VAL);
        }
        while (exponent--) {
            value = minus ? value * 0.1 : value * 10;
        }
    }
    return negative ? -value : value;
}
extern "C" unsigned long __wrap_strtoul(const char* text, char** end, int base) {
    const auto* start = text;
    if (!text || (base != 0 && (base < 2 || base > 36))) {
        if (end) {
            *end = const_cast<char*>(text);
        }
        return 0;
    }
    while (*text == ' ' || *text == '\t') {
        ++text;
    }
    const bool negative = *text == '-';
    if (*text == '-' || *text == '+') {
        ++text;
    }
    if ((base == 0 || base == 16) && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = 16;
        text += 2;
    }
    if (!base) {
        base = *text == '0' ? 8 : 10;
    }
    unsigned long result = 0;
    bool any = false, overflow = false;
    for (;;) {
        const auto c = *text;
        const unsigned digit = c >= '0' && c <= '9'   ? c - '0'
                               : c >= 'a' && c <= 'z' ? c - 'a' + 10
                               : c >= 'A' && c <= 'Z' ? c - 'A' + 10
                                                      : 36;
        if (digit >= static_cast<unsigned>(base)) {
            break;
        }
        any = true;
        if (result > (ULONG_MAX - digit) / base) {
            overflow = true;
        } else {
            result = result * base + digit;
        }
        ++text;
    }
    if (end) {
        *end = const_cast<char*>(any ? text : start);
    }
    if (overflow) {
        localErrno = 34;
        return ULONG_MAX;
    }
    return negative ? 0 - result : result;
}

// Native側で参照するscanはOpenGL versionの整数2個だけに限定する。
// INI読込とImGui入力は無効。未知形式は結果0で拒否し、libcのheap/FSへ逃がさない。
extern "C" int __wrap_sscanf(const char* text, const char* format, ...) {
    if (!text || !format) {
        return 0;
    }
    const bool shader = std::strcmp(format, "#version %d") == 0;
    if (shader) {
        if (std::strncmp(text, "#version ", 9) != 0) {
            return 0;
        }
        text += 9;
    } else if (std::strcmp(format, "%d.%d") != 0) {
        return 0;
    }
    int values[2]{};
    for (unsigned i = 0; i < (shader ? 1u : 2u); ++i) {
        unsigned digits = 0;
        while (*text >= '0' && *text <= '9' && digits < 3) {
            values[i] = values[i] * 10 + (*text++ - '0');
            ++digits;
        }
        if (!digits || (!shader && i == 0 && *text++ != '.')) {
            return 0;
        }
    }
    va_list args;
    va_start(args, format);
    auto* major = va_arg(args, int*);
    auto* minor = shader ? nullptr : va_arg(args, int*);
    va_end(args);
    if (!major || (!shader && !minor)) {
        return 0;
    }
    *major = values[0];
    if (minor) {
        *minor = values[1];
    }
    return shader ? 1 : 2;
}
