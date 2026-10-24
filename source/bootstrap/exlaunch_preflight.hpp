#pragma once

#include <cstdint>

namespace yw1::bootstrap {

// 読取専用のkernel照会だけで、固定exlaunchが要求する前提を確認する。
std::uint32_t checkExlaunchEnvironment(const void* moduleStart);

} // 名前空間 yw1::bootstrap
