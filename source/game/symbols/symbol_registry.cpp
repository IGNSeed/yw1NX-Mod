#include "symbol_registry.hpp"

#include <limits>

namespace yw1::game {

SymbolRegistry::SymbolRegistry(std::span<const SymbolRecord> symbols) : symbols_(symbols) {}

std::optional<std::uint64_t> SymbolRegistry::resolve(std::string_view name,
                                                   const MatchedVersion& version,
                                                   const ModuleImage& image) const {
    const auto& profile = version.profile();
    constexpr auto kMaximum = std::numeric_limits<std::uint64_t>::max();
    if (name.empty() || image.size == 0 || image.base > kMaximum - image.size ||
        image.identity.titleId != profile.titleId ||
        image.identity.moduleName != profile.moduleName ||
        image.identity.fullBuildId != profile.fullBuildId) {
        return std::nullopt;
    }
    const SymbolRecord* match = nullptr;
    for (const auto& symbol : symbols_) {
        if (symbol.name == name && symbol.moduleName == profile.moduleName &&
            symbol.fullBuildId == profile.fullBuildId) {
            if (match != nullptr) {
                return std::nullopt;
            }
            match = &symbol;
        }
    }
    if (match == nullptr || match->evidence != EvidenceStatus::Verified || match->size == 0 ||
        match->alignment == 0 || (match->alignment & (match->alignment - 1)) != 0 ||
        match->relativeOffset >= image.size || match->size > image.size - match->relativeOffset) {
        return std::nullopt;
    }
    const auto address = image.base + match->relativeOffset;
    if (address % match->alignment != 0) {
        return std::nullopt;
    }
    return address;
}

} // 名前空間 yw1::game
