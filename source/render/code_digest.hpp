#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace yw1::render {
using CodeDigest = std::array<std::uint8_t, 32>;
CodeDigest codeDigest(std::span<const std::byte> bytes);
} // 名前空間 yw1::render
