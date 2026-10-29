#pragma once
#include <cstdint>

namespace yw1::render {
enum class MemoryObligation : std::uint64_t {
    ContextConstruction = 1, VectorGrowth = 2, FontAtlas = 4, DemoBranches = 8
};
// 固定上流のNULL非対応経路。Hostで有限回動いたことや容量から証明済みへ遷移させない。
constexpr std::uint64_t nativeMemoryBlockMask(unsigned mode) {
    constexpr auto common = static_cast<std::uint64_t>(MemoryObligation::ContextConstruction) |
        static_cast<std::uint64_t>(MemoryObligation::VectorGrowth) |
        static_cast<std::uint64_t>(MemoryObligation::FontAtlas);
    return mode == 1 ? common : mode == 2 ?
        common | static_cast<std::uint64_t>(MemoryObligation::DemoBranches) : 0;
}
// mode0は描画要求なし。未成立のmodeを既存Host専用許可へ変換しない。
constexpr bool nativeMemoryAdmitted(unsigned mode) {
    return mode > 0 && mode <= 2 && nativeMemoryBlockMask(mode) == 0;
}
// B0214の明示的な開発用許可。完全OOM回復とは別で、通常経路の容量を検査する。
constexpr bool normalPathExperimentAdmitted(unsigned mode, bool requested,
    std::uint64_t capacity, std::uint64_t largestFree, bool initialized) {
    return requested && (mode == 1 || mode == 2) && capacity >= 1048576 &&
        largestFree >= (initialized ? 262144 : 786432);
}
} // 名前空間 yw1::render
