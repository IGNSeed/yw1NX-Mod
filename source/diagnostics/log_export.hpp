#pragma once

#include "log_export_wire.hpp"

#include <atomic>

namespace yw1::diagnostics {

struct alignas(64) LogExportState {
    std::uint64_t magic = kLogExportMagic;
    std::uint32_t abiVersion = kLogExportAbi;
    std::uint32_t capacity = kExportCapacity;
    std::uint64_t buildTag = kBuildTag;
    std::atomic<std::uint64_t> generation{0};
    std::atomic<std::uint32_t> used{0};
    std::atomic<std::uint32_t> dropped{0};
    std::atomic<std::uint32_t> flags{0};
    std::atomic<std::uint32_t> checksum{kFnvBasis};
    std::atomic<std::uint64_t> bootToken{0};
    std::uint64_t reserved = 0;
};
static_assert(sizeof(LogExportState) == sizeof(ExportedLogHeader));
static_assert(offsetof(LogExportState, checksum) == offsetof(ExportedLogHeader, checksum));
static_assert(offsetof(LogExportState, generation) == 24);
static_assert(offsetof(LogExportState, used) == 32);
static_assert(offsetof(LogExportState, dropped) == 36);
static_assert(offsetof(LogExportState, flags) == 40);
static_assert(offsetof(LogExportState, bootToken) == 48);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

extern "C" {
extern LogExportState yw1_module_log_header;
extern std::atomic<std::uint64_t> yw1_module_log_words[kExportCapacity / 8];
}

void setExportBootToken(std::uint64_t token);
// 呼出元MemoryLogのlock内だけで実行する。prefix追記分だけをコピーしhashを更新する。
void publishLogAppend(const char* text, std::size_t size, std::uint32_t dropped, bool sealed);

} // 名前空間 yw1::diagnostics
