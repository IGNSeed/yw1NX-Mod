#pragma once
#include "diagnostics/callback_wire.hpp"
namespace yw1::companion {
struct Report;
struct Observation;
void appendCallbackReport(Report& report, const Observation& observation);
}
