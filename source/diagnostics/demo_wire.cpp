#include "demo_wire.hpp"

namespace yw1::diagnostics {
// 不変の記述子だけを追加する。Callback、heap、GL、FSには処理を追加しない。
extern "C" alignas(8) constinit const DemoWire yw1_demo_diagnostics =
    initialDemoWire(YW1_DEMO_PROFILE);
} // 名前空間 yw1::diagnostics
