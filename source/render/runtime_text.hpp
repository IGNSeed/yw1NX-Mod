#pragma once
#include <cstddef>
namespace yw1::render::runtime {
std::size_t length(const char* text);
int compare(const char* a, const char* b, std::size_t count);
const char* findChar(const char* text, int value);
const char* findText(const char* text, const char* wanted);
void* move(void* to, const void* from, std::size_t count);
void sort(void* data, std::size_t count, std::size_t width,
          int (*compare)(const void*, const void*));
}
