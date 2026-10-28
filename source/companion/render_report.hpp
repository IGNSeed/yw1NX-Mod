#pragma once
namespace yw1::companion {
struct Report;
struct Observation;
void appendRenderReport(Report& report, const Observation& observation);
void showRenderPage(const Observation& observation, bool memoryPage);
} // 名前空間 yw1::companion
