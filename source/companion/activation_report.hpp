#pragma once
namespace yw1::companion {
struct Observation;
struct Report;
void appendActivationReport(Report& report, const Observation& observation);
void showActivationPage(const Observation& observation);
} // 名前空間 yw1::companion
