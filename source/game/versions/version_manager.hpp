#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace yw1::game {

using BuildId = std::array<std::uint8_t, 32>;

struct VersionProfile {
    std::uint64_t titleId;
    std::string_view gameVersion;
    std::string_view moduleName;
    BuildId fullBuildId;
};

struct ModuleIdentity {
    std::uint64_t titleId;
    std::string_view moduleName;
    BuildId fullBuildId;
};

class MatchedVersion {
public:
    const VersionProfile& profile() const;

private:
    friend class VersionManager;
    explicit MatchedVersion(const VersionProfile& profile);
    const VersionProfile* profile_;
};

class VersionManager {
public:
    // プロファイルの所有権は呼び出し元にあり、照合結果より長く保持する。
    explicit VersionManager(std::span<const VersionProfile> profiles);
    std::optional<MatchedVersion> match(const ModuleIdentity& identity) const;

private:
    std::span<const VersionProfile> profiles_;
};

} // 名前空間 yw1::game
