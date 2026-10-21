#include "memory_log.hpp"

namespace yw1::logging {
namespace {
constinit MemoryLog log;
}

MemoryLog& memoryLog() {
    return log;
}

std::size_t formatResult(char* out, std::size_t capacity, const char* label, std::size_t length,
                         Result result) {
    if (out == nullptr || label == nullptr || length > capacity || capacity - length < 11) {
        return 0;
    }
    for (std::size_t i = 0; i < length; ++i) {
        out[i] = label[i];
    }
    out[length++] = '0';
    out[length++] = 'x';
    constexpr char kDigits[] = "0123456789ABCDEF";
    for (unsigned int i = 0; i < 8; ++i) {
        out[length++] = kDigits[(result >> ((7 - i) * 4)) & 15];
    }
    out[length++] = '\n';
    return length;
}

bool MemoryLog::append(const char* text, std::size_t size) {
    if (lock_.test_and_set(std::memory_order_acquire)) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    const bool accepted = !sealed_ && text != nullptr && size != 0 && size <= kLogCapacity - used_;
    if (accepted) {
        for (std::size_t i = 0; i < size; ++i) {
            data_[used_ + i] = text[i];
        }
        used_ += size;
    } else {
        dropped_.fetch_add(1, std::memory_order_relaxed);
    }
    lock_.clear(std::memory_order_release);
    return accepted;
}

bool MemoryLog::appendResult(const char* label, std::size_t size, Result result) {
    char line[256];
    const auto length = formatResult(line, sizeof(line), label, size, result);
    return append(line, length);
}

bool MemoryLog::seal(LogView& view) {
    if (lock_.test_and_set(std::memory_order_acquire)) {
        return false;
    }
    sealed_ = true;
    view = {data_, used_};
    lock_.clear(std::memory_order_release);
    return true;
}

} // 名前空間 yw1::logging
