#pragma once

#include "diagnostics/egl_hook_guard.hpp"
#include <atomic>
#include <cstdint>
#include <span>

namespace yw1::hooking {

using Result = std::uint32_t;
constexpr Result kNotAttempted = UINT32_MAX;
enum class HookState : std::uint32_t {
    Disabled,
    PreflightPassed,
    TrampolinePrepared,
    OriginalPublished,
    InstallCommitted,
    Active,
    Failed
};
enum class HookBackend : std::uint32_t { None, ExportTrampoline };
enum class HookError : Result {
    Busy = 0xE201,
    AlreadyAttempted,
    Identity,
    Instructions,
    Boundary,
    Capacity,
    InvalidRegion,
    InvalidBranch,
    UnverifiedThread,
    InvalidPlatform,
    Readback,
    UnsafeRollback,
    PoolExhausted
};
constexpr Result error(HookError value) {
    return static_cast<Result>(value);
}

// Resultはプラットフォームの実値、状態とblockedReasonはMOD独自の分類。
struct InstallerReport {
    HookBackend backend = HookBackend::ExportTrampoline;
    HookState state = HookState::Disabled;
    Result preflight = kNotAttempted, identity = kNotAttempted, instructions = kNotAttempted;
    Result trampoline = kNotAttempted, publish = kNotAttempted;
    std::uint32_t quiescenceVerified = 0;
    Result mapping = kNotAttempted, cache = kNotAttempted;
    std::uint32_t commitAttempted = 0;
    Result commit = kNotAttempted;
    std::uint32_t rollbackAttempted = 0;
    Result rollback = kNotAttempted;
    std::uint32_t active = 0, blockedReason = 0;
    Result cleanup = kNotAttempted;
    std::uint32_t resourcesRetained = 0;
    Result leaseRelease = kNotAttempted;
};
static_assert(sizeof(InstallerReport) == 76);

struct HookTarget {
    game::ModuleImage module;
    std::uint64_t executableSize;
    std::uint64_t offset = 0x10B60;
    std::uint64_t functionSize = 72;
    std::uintptr_t callback = 0;
};
struct BranchPlan {
    std::uint32_t original[5]{}, patch[5]{}, trampoline[12]{};
    std::uint32_t patchWords = 0, trampolineWords = 0;
};
Result prepareBranch(const HookTarget& target, std::uintptr_t trampolineRx, std::size_t capacity,
                     BranchPlan& output);
// 同じ距離・整列検査を、固定callsiteの単一BLにも使用する。
Result encodeCallBranch(std::uintptr_t from, std::uintptr_t to, std::uint32_t& output);
Result preflight(const HookTarget& target, std::span<const std::uint32_t> code,
                 InstallerReport& report);

// 公開後のslotはprocess寿命まで再利用しない。20番目までで、index20は絶対に渡さない。
class TrampolineSlots {
  public:
    static constexpr std::uint32_t kCapacity = 20;
    Result reserve(std::uint32_t& index);

  private:
    std::atomic<std::uint32_t> next_{0};
};

struct HookRegion {
    std::uintptr_t rx = 0;
    std::uint32_t* rw = nullptr;
    std::size_t capacity = 0;
};

// 実機adapterは未提供。leaseは対象・callback・trampolineへの全entryを隔離する契約。
// validLeaseは全操作を通じて失効しない証拠を再確認する。単なるbool設定では代用しない。
// mapJitは非重複RW/RX同一物理alias、syncCodeはRXを含む全実行CPUへの可視性を保証する。
// patchは全expectedを比較し、short CASの失敗もResultへ返す。longは有効lease下だけ扱う。
// 失敗時の所有権は保持し、abortせず、後述cleanupの成功まで暗黙解放しない。
// allocateJit失敗・mapTarget失敗は未取得、mapJit失敗はallocate済領域を維持する契約。
// callback ABI/RX帰属と全lease契約を実装・検証できないadapterは接続禁止。
class HookPlatform {
  public:
    // 完全identity・実行範囲・callback ABIを検証し、以後の操作をこのtargetだけへ束縛する。
    virtual Result bindTarget(const HookTarget& target) = 0;
    virtual Result acquireLease() = 0;
    virtual bool validLease() = 0;
    virtual Result releaseLease() = 0;
    virtual Result readTarget(std::span<std::uint32_t> output) = 0;
    virtual Result allocateJit(HookRegion& region) = 0;
    virtual Result mapJit(HookRegion& region) = 0;
    virtual Result mapTarget() = 0;
    virtual Result syncCode(bool target) = 0;
    virtual Result patch(std::span<const std::uint32_t> expected,
                         std::span<const std::uint32_t> replacement) = 0;
    virtual Result unmapTarget() = 0;
    virtual Result freeJit() = 0;

  protected:
    ~HookPlatform() = default;
};

// 公開したOriginalはFailedでも保持する。稼働中のunhook・destructorによる復元は提供しない。
// インストーラーとplatform/resourceの寿命はprocess全体。失敗後の再試行も拒否する。
class SafeHookInstaller {
  public:
    Result install(const HookTarget& target, HookPlatform& platform);
    std::uintptr_t original() const {
        return original_.load(std::memory_order_acquire);
    }
    HookState state() const {
        return state_.load(std::memory_order_acquire);
    }
    bool readReport(InstallerReport& report);

  private:
    void setState(HookState value);
    std::atomic_flag busy_ = ATOMIC_FLAG_INIT;
    std::atomic<std::uintptr_t> original_{0};
    std::atomic<HookState> state_{HookState::Disabled};
    InstallerReport report_{};
};
static_assert(std::atomic<std::uintptr_t>::is_always_lock_free);
} // 名前空間 yw1::hooking
