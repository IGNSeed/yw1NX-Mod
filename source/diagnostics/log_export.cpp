#include "log_export.hpp"

namespace yw1::diagnostics {

extern "C" {
alignas(4096) constinit LogExportState yw1_module_log_header{};
alignas(4096) constinit std::atomic<std::uint64_t> yw1_module_log_words[kExportCapacity / 8]{};
}

void setExportBootToken(std::uint64_t token) {
    std::uint64_t empty = 0;
    static_cast<void>(yw1_module_log_header.bootToken.compare_exchange_strong(empty, token));
}

void publishLogAppend(const char* text, std::size_t size, std::uint32_t dropped, bool sealed) {
    auto& header = yw1_module_log_header;
    auto used = header.used.load(std::memory_order_relaxed);
    if (size > kExportCapacity - used || (size != 0 && text == nullptr)) {
        return;
    }
    // acquire/release RMWでoddを先に公開し、payload storeより後へ移動させない。
    const auto previous = header.generation.fetch_add(1, std::memory_order_acq_rel);
    auto hash = header.checksum.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < size;) {
        const auto index = used / 8;
        auto word = yw1_module_log_words[index].load(std::memory_order_relaxed);
        do {
            const auto shift = (used % 8) * 8;
            const auto byte = static_cast<unsigned char>(text[i++]);
            word = (word & ~(std::uint64_t{255} << shift)) | (std::uint64_t{byte} << shift);
            hash = (hash ^ byte) * 16777619u;
            ++used;
        } while (i < size && used % 8 != 0);
        yw1_module_log_words[index].store(word, std::memory_order_release);
    }
    auto flags = used != 0 ? kExportInitialized : 0u;
    flags |= sealed ? kExportSealed : 0u;
    flags |= dropped != 0 ? kExportDropped : 0u;
    header.used.store(used, std::memory_order_relaxed);
    header.dropped.store(dropped, std::memory_order_relaxed);
    header.flags.store(flags, std::memory_order_relaxed);
    header.checksum.store(hash, std::memory_order_relaxed);
    header.generation.store(previous + 2, std::memory_order_release);
}

} // 名前空間 yw1::diagnostics
