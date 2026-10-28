#pragma once
namespace yw1::companion {
struct Report;
struct Observation;
void appendContextReport(Report& report, const Observation& observation);
void showContextPage(const Observation& observation, bool glPage);
} // 名前空間 yw1::companion
