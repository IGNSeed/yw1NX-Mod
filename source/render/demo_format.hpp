#pragma once
#include "imgui_internal.h"
#include <cstddef>
namespace yw1::render {
// 固定公式Demoの配列サイズを型から取得する。汎用sprintfやheapを使用しない。
template <std::size_t N, typename... Args>
int demoFormat(char (&buffer)[N], const char* format, Args... args) {
    return ImFormatString(buffer, N, format, args...);
}
}
