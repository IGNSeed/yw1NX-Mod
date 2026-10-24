#include "safe_hook_installer.hpp"
#include <cstring>
#include <limits>

namespace yw1::hooking {
namespace {
// 別Installer同士も同時に設置しない。ゲームThreadの隔離を代用するlockではない。
constinit std::atomic_flag installCoordinator = ATOMIC_FLAG_INIT;
bool same(std::span<const std::uint32_t> left, std::span<const std::uint32_t> right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
        if (left[i] != right[i]) {
            return false;
        }
    }
    return true;
}
bool encodeBranch(std::uintptr_t from, std::uintptr_t to, std::uint32_t& instruction) {
    if ((from | to) % 4 != 0) {
        return false;
    }
    const bool positive = to >= from;
    const auto distance = positive ? to - from : from - to;
    if (distance > (positive ? 0x7FFFFFCu : 0x8000000u)) {
        return false;
    }
    const auto words = static_cast<std::uint32_t>(distance / 4);
    instruction = 0x14000000u | ((positive ? words : 0u - words) & 0x03FFFFFFu);
    return true;
}
} // 無名名前空間

Result preflight(const HookTarget& target, std::span<const std::uint32_t> code,
                 InstallerReport& report) {
    report.identity = diagnostics::resolveSwap(target.module) ? 0 : error(HookError::Identity);
    Result result = report.identity;
    if (result == 0 && (target.offset != 0x10B60 || target.functionSize != 72 ||
                        target.callback == 0 || target.callback % 4 != 0)) {
        result = error(HookError::Boundary);
    }
    if (result == 0) {
        const auto guard =
            diagnostics::checkSwap(target.module, target.offset, target.executableSize, code);
        report.instructions = guard == diagnostics::HookGuardStatus::Passed
                                  ? 0
                                  : error(guard == diagnostics::HookGuardStatus::InvalidInstructions
                                              ? HookError::Instructions
                                              : HookError::Boundary);
        result = report.instructions;
    }
    report.preflight = result;
    return result;
}

Result prepareBranch(const HookTarget& target, std::uintptr_t trampolineRx, std::size_t capacity,
                     BranchPlan& output) {
    if (!diagnostics::resolveSwap(target.module) || target.offset != 0x10B60 ||
        target.functionSize != 72) {
        return error(HookError::Identity);
    }
    const auto address = target.module.base + target.offset;
    if (trampolineRx == 0 || trampolineRx % 8 != 0 || target.callback == 0 ||
        target.callback % 4 != 0 || trampolineRx > UINTPTR_MAX - sizeof(output.trampoline)) {
        return error(HookError::InvalidBranch);
    }
    if ((trampolineRx < address + 72 && address < trampolineRx + sizeof(output.trampoline)) ||
        (target.callback >= address && target.callback < address + 72) ||
        (target.callback >= trampolineRx &&
         target.callback < trampolineRx + sizeof(output.trampoline))) {
        return error(HookError::InvalidBranch);
    }
    output = {};
    for (unsigned i = 0; i < 5; ++i) {
        output.original[i] = output.patch[i] = diagnostics::kSwapPrefix[i];
    }
    if (encodeBranch(address, target.callback, output.patch[0])) {
        output.patchWords = 1;
    } else {
        // 8byte alignedの固定候補だけを扱い、未確認の5命令/PC相対変換へ一般化しない。
        if (address % 8 != 0) {
            return error(HookError::InvalidBranch);
        }
        output.patchWords = 4;
        output.patch[0] = 0x58000051;
        output.patch[1] = 0xD61F0220;
        output.patch[2] = static_cast<std::uint32_t>(target.callback);
        output.patch[3] = static_cast<std::uint32_t>(target.callback >> 32);
    }
    unsigned words = output.patchWords;
    for (unsigned i = 0; i < words; ++i) {
        output.trampoline[i] = output.original[i];
    }
    if (words % 2 != 0) {
        output.trampoline[words++] = 0xD503201F;
    }
    output.trampoline[words++] = 0x58000051;
    output.trampoline[words++] = 0xD61F0220;
    const auto resume = address + output.patchWords * 4;
    output.trampoline[words++] = static_cast<std::uint32_t>(resume);
    output.trampoline[words++] = static_cast<std::uint32_t>(resume >> 32);
    output.trampolineWords = words;
    return capacity >= words * 4 ? 0 : error(HookError::Capacity);
}

Result TrampolineSlots::reserve(std::uint32_t& index) {
    auto next = next_.load(std::memory_order_relaxed);
    for (unsigned attempt = 0; attempt <= kCapacity; ++attempt) {
        if (next >= kCapacity) {
            return error(HookError::PoolExhausted);
        }
        if (next_.compare_exchange_weak(next, next + 1, std::memory_order_relaxed)) {
            index = next;
            return 0;
        }
    }
    return error(HookError::Busy);
}

void SafeHookInstaller::setState(HookState value) {
    report_.state = value;
    state_.store(value, std::memory_order_release);
}
bool SafeHookInstaller::readReport(InstallerReport& report) {
    if (busy_.test_and_set(std::memory_order_acquire)) {
        return false;
    }
    report = report_;
    busy_.clear(std::memory_order_release);
    return true;
}

Result SafeHookInstaller::install(const HookTarget& target, HookPlatform& platform) {
    if (busy_.test_and_set(std::memory_order_acquire)) {
        return error(HookError::Busy);
    }
    if (state() != HookState::Disabled) {
        busy_.clear(std::memory_order_release);
        return error(HookError::AlreadyAttempted);
    }
    if (installCoordinator.test_and_set(std::memory_order_acquire)) {
        busy_.clear(std::memory_order_release);
        return error(HookError::Busy);
    }
    std::uint32_t observed[5]{};
    BranchPlan plan;
    HookRegion region;
    bool lease = false, allocated = false, mapped = false, changed = false;
    report_.identity = diagnostics::resolveSwap(target.module) ? 0 : error(HookError::Identity);
    Result result = report_.identity;
    if (result == 0 && (target.offset != 0x10B60 || target.functionSize != 72)) {
        result = error(HookError::Boundary);
    }
    if (result == 0) {
        result = platform.bindTarget(target);
    }
    if (result == 0) {
        result = platform.readTarget(observed);
    }
    if (result == 0) {
        result = preflight(target, observed, report_);
    } else {
        report_.preflight = result;
    }
    if (result == 0) {
        setState(HookState::PreflightPassed);
        result = platform.acquireLease();
        lease = result == 0;
        if (result == 0 && !platform.validLease()) {
            result = error(HookError::UnverifiedThread);
        }
        report_.quiescenceVerified = result == 0;
    }
    if (result == 0) {
        result = platform.allocateJit(region);
        allocated = result == 0;
        report_.trampoline = result;
    }
    if (result == 0) {
        result = platform.mapJit(region);
    }
    if (result == 0) {
        if (!region.rw || reinterpret_cast<std::uintptr_t>(region.rw) % 4 != 0 ||
            reinterpret_cast<std::uintptr_t>(region.rw) == region.rx) {
            result = error(HookError::InvalidRegion);
        } else {
            result = prepareBranch(target, region.rx, region.capacity, plan);
        }
    }
    if (result == 0) {
        std::memcpy(region.rw, plan.trampoline, plan.trampolineWords * 4);
        if (!same({region.rw, plan.trampolineWords}, {plan.trampoline, plan.trampolineWords})) {
            result = error(HookError::Readback);
        } else {
            result = platform.syncCode(false);
            report_.cache = result;
        }
    }
    if (allocated) {
        report_.trampoline = result;
    }
    if (result == 0) {
        setState(HookState::TrampolinePrepared);
        report_.mapping = platform.mapTarget();
        result = report_.mapping;
        mapped = result == 0;
    }
    if (result == 0) {
        result = platform.readTarget(observed);
        if (result == 0 && !same(observed, plan.original)) {
            result = error(HookError::Instructions);
        }
        if (result == 0 && !platform.validLease()) {
            result = error(HookError::UnverifiedThread);
        }
    }
    if (result == 0) {
        // このrelease以降のOriginal/RX資源は、失敗やrollback後もprocess寿命まで保持する。
        original_.store(region.rx, std::memory_order_release);
        report_.publish = 0;
        setState(HookState::OriginalPublished);
        if (!platform.validLease()) {
            result = error(HookError::UnverifiedThread);
        } else {
            report_.commitAttempted = 1;
            changed = true;
            result =
                platform.patch({plan.original, plan.patchWords}, {plan.patch, plan.patchWords});
            report_.commit = result;
        }
        if (result == 0) {
            result = platform.readTarget(observed);
            if (result == 0 && !same(observed, plan.patch)) {
                result = error(HookError::Readback);
            }
        }
        if (result == 0) {
            result = platform.syncCode(true);
            report_.cache = result;
        }
        if (result == 0 && !platform.validLease()) {
            result = error(HookError::UnverifiedThread);
        }
        if (report_.commitAttempted) {
            report_.commit = result;
        }
        if (result == 0) {
            setState(HookState::InstallCommitted);
        }
    }
    if (result != 0 && changed) {
        // lease失効時は、実行中かもしれない命令へ書き戻さない。
        if (mapped && platform.validLease()) {
            const auto read = platform.readTarget(observed);
            if (read == 0 && same(observed, plan.original)) {
                // 書き換え前の失敗なら、不要な復元writeやcache操作を行わない。
                changed = false;
            } else {
                report_.rollback = read;
                for (unsigned i = 0; report_.rollback == 0 && i < 5; ++i) {
                    if (observed[i] != plan.original[i] && observed[i] != plan.patch[i]) {
                        report_.rollback = error(HookError::UnsafeRollback);
                    }
                }
                if (report_.rollback == 0) {
                    report_.rollbackAttempted = 1;
                    report_.rollback = platform.patch({observed, plan.patchWords},
                                                      {plan.original, plan.patchWords});
                }
                if (report_.rollback == 0) {
                    report_.rollback = platform.readTarget(observed);
                    if (report_.rollback == 0 && !same(observed, plan.original)) {
                        report_.rollback = error(HookError::Readback);
                    }
                }
                if (report_.rollback == 0) {
                    report_.rollback = platform.syncCode(true);
                }
                changed = report_.rollback != 0;
            }
        }
        if (changed) {
            report_.blockedReason = error(HookError::UnsafeRollback);
        }
    }
    // commit後のunmap失敗は所有権が不明なので、同じaliasへのrollbackを試さない。
    if (mapped && (result == 0 || !changed)) {
        report_.cleanup = platform.unmapTarget();
        if (report_.cleanup != 0) {
            if (result == 0) {
                result = report_.cleanup;
            }
            changed = true;
        }
    }
    if (allocated && original() == 0) {
        const auto cleanup = platform.freeJit();
        report_.cleanup = cleanup;
        if (result == 0) {
            result = cleanup;
        }
        if (cleanup != 0) {
            report_.resourcesRetained = 1;
        }
    }
    if (lease && (!changed || result == 0)) {
        report_.leaseRelease = platform.releaseLease();
        if (report_.leaseRelease != 0) {
            report_.resourcesRetained = 1;
        }
        if (result == 0) {
            result = report_.leaseRelease;
        }
    } else if (lease) {
        report_.resourcesRetained = 1;
    }
    if (original() != 0) {
        report_.resourcesRetained = 1;
    }
    if (result == 0) {
        report_.active = 1;
        setState(HookState::Active);
    } else {
        if (report_.blockedReason == 0) {
            report_.blockedReason = result;
        }
        setState(HookState::Failed);
    }
    installCoordinator.clear(std::memory_order_release);
    busy_.clear(std::memory_order_release);
    return result;
}
} // 名前空間 yw1::hooking
