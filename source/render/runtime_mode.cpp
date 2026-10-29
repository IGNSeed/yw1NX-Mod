#include "runtime_mode.hpp"

namespace yw1::render {
namespace {
constinit RuntimeModeControl mode;
} // 無名名前空間

RuntimeModeControl& runtimeModeControl() { return mode; }

} // 名前空間 yw1::render
