#pragma once
#include <cstdint>
#include <limits>

namespace yw1::render {
// 周波数は実counterから取得する。積のoverflowと未初期化時刻を拒否する。
constexpr std::uint64_t ticksToNanoseconds(std::uint64_t ticks, std::uint64_t frequency) {
    constexpr auto billion = std::uint64_t{1000000000};
    if (!frequency || frequency > billion || !ticks || ticks / frequency > UINT64_MAX / billion) {
        return 0;
    }
    const auto whole = ticks / frequency * billion;
    const auto remainder = ticks % frequency * billion / frequency;
    return whole > UINT64_MAX - remainder ? 0 : whole + remainder;
}
} // 名前空間 yw1::render
