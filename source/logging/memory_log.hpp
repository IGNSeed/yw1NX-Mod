#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace yw1::logging {

using Result = std::uint32_t;
constexpr std::size_t kLogCapacity = 32 * 1024;
constexpr Result kInvalidResponse = 0xE001;
constexpr Result kDeadline = 0xE002;
constexpr Result kBufferFull = 0xE003;
constexpr Result kFileLimit = 0xE004;
constexpr Result kWrongEntry = 0xE005;
constexpr Result kAlreadyRun = 0xE006;

struct LogView {
    const char* data;
    std::size_t size;
};

// ロック取得失敗時は待たずに欠落数へ加算する。封印後のデータはIPC完了まで不変。
class MemoryLog {
  public:
    bool append(const char* text, std::size_t size);
    bool appendResult(const char* label, std::size_t size, Result result);
    bool seal(LogView& view);
    std::uint32_t dropped() const {
        return dropped_.load(std::memory_order_relaxed);
    }

  private:
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    std::atomic<std::uint32_t> dropped_{0};
    bool sealed_ = false;
    std::size_t used_ = 0;
    // MapAliasの対象ページにロックや長さを同居させない。
    alignas(4096) char data_[kLogCapacity]{};
};

MemoryLog& memoryLog();
std::size_t formatResult(char* out, std::size_t capacity, const char* label, std::size_t length,
                         Result result);

} // 名前空間 yw1::logging
