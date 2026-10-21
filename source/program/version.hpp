#pragma once

#include <common.hpp>

namespace exl::util {

// Bootstrapはゲーム関数の再配置表を使用しない。
enum class UserVersion { Bootstrap };

namespace impl {

ALWAYS_INLINE UserVersion DetermineUserVersion() {
    return UserVersion::Bootstrap;
}

} // 名前空間 impl
} // 名前空間 exl::util
