#pragma once

#include <common.hpp>
#include <tuple>

#include "version.hpp"

namespace exl::reloc {

// 未検証のゲームシンボルへ書き込まない空の再配置表。
using VersionType = util::UserVersion;
using UserTableSet = TableSet<VersionType>;

} // 名前空間 exl::reloc
