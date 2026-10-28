#include "imgui_arena.hpp"
#include <new>

namespace yw1::render {
bool ImGuiArena::initialize(std::span<std::byte> storage) {
    if (!storage_.empty() || storage.size() < sizeof(Block) + kAlign ||
        reinterpret_cast<std::uintptr_t>(storage.data()) % kAlign || storage.size() % kAlign) {
        return false;
    }
    storage_ = storage;
    stats_.capacity = storage.size();
    ::new (storage.data()) Block{storage.size(), 0, kMagic, 0};
    return true;
}
ImGuiArena::Block* ImGuiArena::block(std::size_t offset) const {
    return std::launder(reinterpret_cast<Block*>(storage_.data() + offset));
}
bool ImGuiArena::valid(std::size_t offset, const Block& b) const {
    return b.magic == kMagic && b.allocated <= 1 && b.span >= sizeof(Block) &&
        b.span % kAlign == 0 && b.span <= storage_.size() - offset &&
        b.requested <= b.span - sizeof(Block);
}
void* ImGuiArena::allocate(std::size_t size) {
    if (!size || size > SIZE_MAX - (kAlign - 1) - sizeof(Block)) {
        ++stats_.failures; return nullptr;
    }
    const auto payload = (size + kAlign - 1) / kAlign * kAlign;
    const auto need = payload + sizeof(Block);
    for (std::size_t offset = 0; offset + sizeof(Block) <= storage_.size();) {
        auto* b = block(offset);
        if (!valid(offset, *b)) { ++stats_.failures; return nullptr; }
        if (!b->allocated && b->span >= need) {
            if (b->span - need >= sizeof(Block) + kAlign) {
                ::new (storage_.data() + offset + need) Block{b->span - need, 0, kMagic, 0};
                b->span = need;
            }
            b->allocated = 1; b->requested = size;
            stats_.used += size; stats_.reserved += b->span;
            if (stats_.used > stats_.peak) { stats_.peak = stats_.used; }
            ++stats_.allocations; ++stats_.live;
            return storage_.data() + offset + sizeof(Block);
        }
        offset += b->span;
    }
    ++stats_.failures;
    return nullptr;
}
bool ImGuiArena::release(void* pointer) {
    if (!pointer) { return true; }
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    const auto base = reinterpret_cast<std::uintptr_t>(storage_.data());
    if (address < base || address - base >= storage_.size()) {
        ++stats_.invalidFrees; return false;
    }
    Block* previous = nullptr;
    for (std::size_t offset = 0; offset + sizeof(Block) <= storage_.size();) {
        auto* b = block(offset);
        if (!valid(offset, *b)) { ++stats_.invalidFrees; return false; }
        if (storage_.data() + offset + sizeof(Block) == pointer) {
            if (!b->allocated) { ++stats_.invalidFrees; return false; }
            stats_.used -= b->requested; stats_.reserved -= b->span;
            ++stats_.frees; --stats_.live; b->allocated = 0; b->requested = 0;
            const auto nextOffset = offset + b->span;
            if (nextOffset + sizeof(Block) <= storage_.size()) {
                auto* next = block(nextOffset);
                if (!valid(nextOffset, *next)) { ++stats_.invalidFrees; return false; }
                if (!next->allocated) { b->span += next->span; }
            }
            if (previous && !previous->allocated) { previous->span += b->span; }
            return true;
        }
        previous = b; offset += b->span;
    }
    ++stats_.invalidFrees;
    return false;
}
std::size_t ImGuiArena::largestFree() const {
    std::size_t largest = 0;
    for (std::size_t offset = 0; offset + sizeof(Block) <= storage_.size();) {
        const auto* b = block(offset);
        if (!valid(offset, *b)) { return 0; }
        if (!b->allocated && b->span - sizeof(Block) > largest) {
            largest = b->span - sizeof(Block);
        }
        offset += b->span;
    }
    return largest;
}
void* ImGuiArena::imguiAllocate(std::size_t size, void* owner) {
    return owner ? static_cast<ImGuiArena*>(owner)->allocate(size) : nullptr;
}
void ImGuiArena::imguiFree(void* pointer, void* owner) {
    if (owner) { static_cast<void>(static_cast<ImGuiArena*>(owner)->release(pointer)); }
}
} // 名前空間 yw1::render
