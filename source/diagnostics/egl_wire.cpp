#include "egl_wire.hpp"
#include <cstring>
namespace yw1::diagnostics {
extern "C" {
constinit EglPublication yw1_egl_diagnostics{};
}
void publishEglDiagnostic(const EglDiagnosticWire &value) {
    // 単一producerから公開し、NROは偶数世代の前後一致だけを認定する。
    auto &pub = yw1_egl_diagnostics;
    const auto previous = pub.generation.fetch_add(1, std::memory_order_acq_rel);
    for (std::size_t i = 0; i < sizeof(value) / 8; ++i) {
        std::uint64_t word;
        std::memcpy(&word, reinterpret_cast<const char *>(&value) + i * 8, 8);
        pub.words[i].store(word, std::memory_order_release);
    }
    pub.generation.store(previous + 2, std::memory_order_release);
}
} // 名前空間 yw1::diagnostics
