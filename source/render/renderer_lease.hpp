#pragma once
#include <cstdint>
namespace yw1::render {
bool tryEnterRenderer();
void leaveRenderer();
// 所有ticketの操作はrenderer lease内部だけ。同時に生存できるImGui所有者は一つ。
bool claimRendererOwner(std::uint64_t& ticket);
void releaseRendererOwner(std::uint64_t& ticket);
} // 名前空間 yw1::render
