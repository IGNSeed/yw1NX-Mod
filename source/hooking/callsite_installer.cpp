#include "callsite_installer.hpp"

namespace yw1::hooking {
Result prepareCallsite(const CallsiteTarget& target, CallsitePlan& plan) {
    constexpr auto mainId = std::to_array(diagnostics::kMainBuildId);
    constexpr game::VersionProfile profiles[]{
        {diagnostics::kTitleId, "1.4.0", "main", mainId}};
    const auto version = game::VersionManager(profiles).match(target.main.identity);
    if (!version || !diagnostics::resolveSwap(target.subsdk0)) {
        return error(HookError::Identity);
    }
    constexpr game::SymbolRecord symbols[]{
        {"eglSwapCall", "main", mainId, kSwapCallOffset, 4, 4, game::EvidenceStatus::Verified},
        {"eglSwapPlt", "main", mainId, kSwapPltOffset, 16, 4, game::EvidenceStatus::Verified}};
    const game::SymbolRegistry registry(symbols);
    const auto call = registry.resolve("eglSwapCall", *version, target.main);
    const auto plt = registry.resolve("eglSwapPlt", *version, target.main);
    if (!call || !plt || target.executableSize > target.main.size ||
        !diagnostics::contains(target.main.base, target.executableSize, *plt, 16) ||
        !diagnostics::contains(target.main.base, target.executableSize, *call, 4)) {
        return error(HookError::Boundary);
    }
    if (target.threadPolicy != diagnostics::CallbackThreadPolicy::AtomicDirectBl) {
        return error(HookError::UnverifiedThread);
    }
    plan.target = *call;
    plan.original = *plt;
    auto result = encodeCallBranch(plan.target, plan.original, plan.expected);
    if (result == 0) { result = encodeCallBranch(plan.target, target.callback, plan.replacement); }
    if (result == 0) { result = encodeCallBranch(target.originalSlot, plan.original, plan.originalBranch); }
    if (result == 0 && (target.callback == 0 || target.originalSlot == 0 ||
        (plan.expected & 0xFC000000u) != 0x94000000u ||
        (plan.replacement & 0xFC000000u) != 0x94000000u ||
        plan.expected == plan.replacement)) {
        result = error(HookError::InvalidBranch);
    }
    return result;
}

Result CallsiteInstaller::install(const CallsiteTarget& target, CallsitePlatform& platform,
                                  diagnostics::CallbackWire& report) {
    using Field = diagnostics::CallbackField;
    if (busy_.test_and_set(std::memory_order_acquire)) { return error(HookError::Busy); }
    if (attempted_) {
        busy_.clear(std::memory_order_release);
        return error(HookError::AlreadyAttempted);
    }
    attempted_ = true;
    report[Field::InstallAttempted] = 1;
    CallsitePlan plan;
    auto result = prepareCallsite(target, plan);
    if (result == 0) { result = platform.validate(target); }
    std::uint32_t word = 0;
    if (result == 0) {
        result = platform.readTarget(word);
        if (result == 0 && word != plan.expected) { result = error(HookError::Instructions); }
    }
    report[Field::GuardResult] = result;
    report[Field::Target] = plan.target;
    report[Field::Original] = plan.original;
    report[Field::ThreadPolicy] = static_cast<unsigned>(target.threadPolicy);
    if (result == 0) {
        report[Field::InitializationStage] = 2;
        result = platform.prepareOriginal(plan);
        report[Field::StubResult] = result;
    }
    bool mapped = false, changed = false;
    if (result == 0) {
        report[Field::ResourcesRetained] = 1;
        result = platform.mapTarget();
        report[Field::MappingResult] = result;
        mapped = result == 0;
    }
    if (result == 0) {
        result = platform.readTarget(word);
        if (result == 0 && word != plan.expected) { result = error(HookError::Instructions); }
    }
    if (result == 0) {
        report[Field::OriginalPublished] = 1;
        report[Field::InitializationStage] = 3;
        platform.publish(report);
        report[Field::PatchAttempted] = 1;
        result = platform.exchange(plan.expected, plan.replacement, changed);
        if (result == 0 && !changed) { result = error(HookError::Instructions); }
    }
    if (result == 0) {
        result = platform.synchronizeTarget();
        report[Field::CacheResult] = result;
    }
    if (result == 0) {
        result = platform.readTarget(word);
        if (result == 0 && word != plan.replacement) { result = error(HookError::Readback); }
    }
    if (result != 0 && changed) {
        // 競合する第三者の命令は上書きせず、自分のBLとの一致時だけ戻す。
        report[Field::RollbackAttempted] = 1;
        bool restored = false;
        auto rollback = platform.exchange(plan.replacement, plan.expected, restored);
        if (rollback == 0 && !restored) { rollback = error(HookError::UnsafeRollback); }
        if (rollback == 0) { rollback = platform.synchronizeTarget(); }
        if (rollback == 0) {
            rollback = platform.readTarget(word);
            if (rollback == 0 && word != plan.expected) { rollback = error(HookError::Readback); }
        }
        report[Field::RollbackResult] = rollback;
        changed = rollback != 0;
        // 戻した後も別CPUに残る新BLは実行可能なのでOriginalとcallbackを保持する。
    }
    if (mapped) {
        const auto cleanup = platform.unmapTarget();
        report[Field::CleanupResult] = cleanup;
        if (cleanup != 0) {
            if (result == 0) { result = cleanup; }
            if (changed) { report[Field::HookActive] = 2; }
        }
    }
    if (report[Field::HookActive] != 2) {
        report[Field::HookActive] = result == 0 ? 1 : changed ? 2 : 0;
    }
    report[Field::InstallResult] = report[Field::HookError] = result;
    report[Field::InitializationStage] = result == 0 ? 5 : 6;
    platform.publish(report);
    busy_.clear(std::memory_order_release);
    return result;
}
} // 名前空間 yw1::hooking
