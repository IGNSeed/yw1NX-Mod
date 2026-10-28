#include "gl_resolver.hpp"
#include "diagnostics/egl_hook_guard.hpp"
#include <cstring>

namespace yw1::render {
namespace {
bool range(const game::ModuleImage& m, std::uint64_t relative, std::size_t size) {
    return relative < m.size && size && size <= m.size - relative &&
        m.base <= UINT64_MAX - relative && m.base + relative <= UINT64_MAX - size;
}
bool digest(GlMemory& memory, std::uint64_t address, unsigned size, const CodeDigest& expected) {
    if (!size || size > 512 || size % 4) { return false; }
    std::array<std::byte, 512> first{}, second{};
    auto a = std::span(first).first(size), b = std::span(second).first(size);
    return memory.read(address, a, true) && memory.read(address, b, true) && first == second &&
        codeDigest(a) == expected;
}
bool pointer(GlMemory& memory, std::uint64_t address, std::uint64_t expected) {
    std::uint64_t first = 0, second = 0;
    return memory.read(address, std::as_writable_bytes(std::span(&first, 1)), false) &&
        memory.read(address, std::as_writable_bytes(std::span(&second, 1)), false) &&
        first == expected && second == expected;
}
} // 無名名前空間
ResolveStats GlResolver::resolve(const game::ModuleImage& module,
    std::span<const GlFunctionSpec> specs, GlMemory& memory, std::span<GlProc> output) {
    ResolveStats stats;
    for (auto& p : output) { p = nullptr; }
    const game::VersionProfile profile{0x0100C0000CEEA000, "1.4.0", "subsdk0",
                                       diagnostics::kSubsdk0Id};
    const auto matched = game::VersionManager(std::span(&profile, 1)).match(module.identity);
    if (!matched) { stats.result = ResolveResult::Identity; return stats; }
    if (specs.empty() || specs.size() > kMaximumFunctions || output.size() != specs.size()) {
        stats.result = ResolveResult::Bounds; return stats;
    }
    constexpr std::uint64_t kTablePointer = 0x1A38180, kTable = 0x1A409E8;
    if (!range(module, kTablePointer, 8) || !range(module, kTable, 8) ||
        !pointer(memory, module.base + kTablePointer, module.base + kTable)) {
        stats.result = ResolveResult::Table; return stats;
    }
    stats.result = ResolveResult::Ready;
    for (unsigned i = 0; i < specs.size(); ++i) {
        const auto& spec = specs[i];
        if (!spec.required) { continue; }
        ++stats.required; ++stats.attempted;
        auto result = ResolveResult::Ready;
        std::size_t length = 0;
        if (spec.name) {
            for (; length < 64; ++length) { if (spec.name[length] == 0) { break; } }
        }
        const std::string_view name{spec.name ? spec.name : "", length};
        const game::SymbolRecord symbol{name, "subsdk0", diagnostics::kSubsdk0Id,
            spec.entry, spec.entrySize, 4, game::EvidenceStatus::Verified};
        const auto address = game::SymbolRegistry(std::span(&symbol, 1)).resolve(
            name, *matched, module);
        if (!length || length == 64 || !address || !range(module, spec.target, spec.targetSize) ||
            spec.slot > 0x10000 || !range(module, kTable + spec.slot, 8)) {
            result = ResolveResult::Bounds;
        } else if (reinterpret_cast<std::uintptr_t>(memory.lookup(spec.name)) != *address) {
            result = ResolveResult::Missing;
        } else if (!digest(memory, *address, spec.entrySize, spec.entryDigest)) {
            result = ResolveResult::Entry;
        } else if (!pointer(memory, module.base + kTable + spec.slot, module.base + spec.target) ||
                   !digest(memory, module.base + spec.target, spec.targetSize, spec.targetDigest)) {
            result = ResolveResult::Target;
        }
        if (result == ResolveResult::Ready) {
            output[i] = reinterpret_cast<GlProc>(*address); ++stats.verified;
            stats.verifiedMask |= std::uint64_t{1} << i;
        } else {
            ++stats.rejected;
            stats.rejectedMask |= std::uint64_t{1} << i;
            if (stats.firstFailure == UINT32_MAX) { stats.firstFailure = i; stats.result = result; }
        }
    }
    if (stats.result != ResolveResult::Ready) {
        // 部分的に成功した表を描画側へ渡さない。
        for (auto& p : output) { p = nullptr; }
    }
    return stats;
}
} // 名前空間 yw1::render
