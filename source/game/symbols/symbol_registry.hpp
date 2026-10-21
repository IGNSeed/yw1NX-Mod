#pragma once

#include "game/versions/version_manager.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace yw1::game {

enum class EvidenceStatus { Candidate, Verified };

struct SymbolRecord {
    std::string_view name;
    std::string_view moduleName;
    BuildId fullBuildId;
    std::uint64_t relativeOffset;
    std::uint64_t size;
    std::uint32_t alignment;
    EvidenceStatus evidence;
};

struct ModuleImage {
    std::uint64_t base;
    std::uint64_t size;
    ModuleIdentity identity;
};

class SymbolRegistry {
public:
    // シンボル一覧は呼び出し元が所有し、レジストリより長く保持する。
    explicit SymbolRegistry(std::span<const SymbolRecord> symbols);
    std::optional<std::uint64_t> resolve(std::string_view name, const MatchedVersion& version,
                                        const ModuleImage& image) const;

private:
    std::span<const SymbolRecord> symbols_;
};

} // 名前空間 yw1::game
