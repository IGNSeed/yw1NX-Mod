#pragma once

#include "module_wire.hpp"

namespace yw1::diagnostics {

constexpr std::uint64_t kLogExportMagic = 0x36474F4C315759ULL;
constexpr std::uint32_t kLogExportAbi = 1;
constexpr std::size_t kExportCapacity = 32 * 1024;
constexpr std::uint32_t kExportInitialized = 1;
constexpr std::uint32_t kExportSealed = 2;
constexpr std::uint32_t kExportDropped = 4;
constexpr std::uint32_t kExportFlags = 7;
constexpr std::uint32_t kFnvBasis = 2166136261u;

struct alignas(64) ExportedLogHeader {
    std::uint64_t magic;
    std::uint32_t abiVersion;
    std::uint32_t capacity;
    std::uint64_t buildTag;
    std::uint64_t generation;
    std::uint32_t used;
    std::uint32_t dropped;
    std::uint32_t flags;
    std::uint32_t checksum;
    std::uint64_t bootToken;
    std::uint64_t reserved;
};
static_assert(sizeof(ExportedLogHeader) == 64);
static_assert(offsetof(ExportedLogHeader, generation) == 24);
static_assert(offsetof(ExportedLogHeader, used) == 32);

inline std::uint32_t logChecksum(const char* text, std::size_t size,
                                 std::uint32_t hash = kFnvBasis) {
    for (std::size_t i = 0; i < size; ++i) {
        hash = (hash ^ static_cast<unsigned char>(text[i])) * 16777619u;
    }
    return hash;
}

inline bool validExportHeader(const ExportedLogHeader& header) {
    return header.magic == kLogExportMagic && header.abiVersion == kLogExportAbi &&
           header.capacity == kExportCapacity && header.buildTag == kBuildTag &&
           header.generation != 0 && (header.generation & 1) == 0 &&
           header.used <= kExportCapacity && (header.flags & ~kExportFlags) == 0 &&
           (header.flags & kExportInitialized) != 0 && header.bootToken != 0 &&
           header.reserved == 0;
}

// UTF-8の過長形式・surrogate・範囲外、NUL/CR等を拒否し、元のbyteを正規化せず保持する。
inline bool validExportText(const char* text, std::size_t size) {
    if (text == nullptr || size > kExportCapacity) {
        return false;
    }
    for (std::size_t i = 0; i < size;) {
        const auto first = static_cast<unsigned char>(text[i++]);
        if (first < 0x80) {
            if ((first < 0x20 && first != '\n' && first != '\t') || first == 0x7F) {
                return false;
            }
            continue;
        }
        unsigned remaining;
        std::uint32_t value;
        std::uint32_t minimum;
        if (first >= 0xC2 && first <= 0xDF) {
            remaining = 1;
            value = first & 31;
            minimum = 0x80;
        } else if (first >= 0xE0 && first <= 0xEF) {
            remaining = 2;
            value = first & 15;
            minimum = 0x800;
        } else if (first >= 0xF0 && first <= 0xF4) {
            remaining = 3;
            value = first & 7;
            minimum = 0x10000;
        } else {
            return false;
        }
        if (remaining > size - i) {
            return false;
        }
        while (remaining-- != 0) {
            const auto byte = static_cast<unsigned char>(text[i++]);
            if ((byte & 0xC0) != 0x80) {
                return false;
            }
            value = (value << 6) | (byte & 63);
        }
        if (value < minimum || value > 0x10FFFF || (value >= 0xD800 && value <= 0xDFFF)) {
            return false;
        }
    }
    return true;
}

} // 名前空間 yw1::diagnostics
