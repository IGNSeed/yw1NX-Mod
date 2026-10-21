#include "version_manager.hpp"

namespace yw1::game {

MatchedVersion::MatchedVersion(const VersionProfile& profile) : profile_(&profile) {}

const VersionProfile& MatchedVersion::profile() const {
    return *profile_;
}

VersionManager::VersionManager(std::span<const VersionProfile> profiles) : profiles_(profiles) {}

std::optional<MatchedVersion> VersionManager::match(const ModuleIdentity& identity) const {
    const BuildId kEmptyBuildId{};
    if (identity.fullBuildId == kEmptyBuildId || identity.moduleName.empty()) {
        return std::nullopt;
    }
    const VersionProfile* match = nullptr;
    for (const auto& profile : profiles_) {
        if (profile.titleId == identity.titleId && profile.moduleName == identity.moduleName &&
            profile.fullBuildId == identity.fullBuildId) {
            // 同じ識別子の重複定義も曖昧な照合として拒否する。
            if (match != nullptr) {
                return std::nullopt;
            }
            match = &profile;
        }
    }
    return match == nullptr ? std::nullopt : std::optional<MatchedVersion>(MatchedVersion(*match));
}

} // 名前空間 yw1::game
