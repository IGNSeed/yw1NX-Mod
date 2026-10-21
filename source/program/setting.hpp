#pragma once

#include <cstddef>

#define EXL_MODULE_NAME "yw1-bootstrap-b001"
#define EXL_USE_FAKEHEAP

namespace exl::setting {

// 上流と同じ専用ヒープ・領域サイズを保持し、ゲームのヒープを使用しない。
constexpr std::size_t HeapSize = 0x5000;
constexpr std::size_t JitSize = 0x1000;
constexpr std::size_t InlinePoolSize = 0x1000;
constexpr std::size_t LogBufferSize = 512;

} // 名前空間 exl::setting
