#pragma once

namespace yw1::companion {
struct Observation;
struct Report;
void appendDemoReport(Report& report, const Observation& observation);
void showDemoPage(const Observation& observation);
} // 名前空間 yw1::companion
