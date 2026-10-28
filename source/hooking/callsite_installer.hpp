#pragma once

#include "safe_hook_installer.hpp"
#include "diagnostics/callback_wire.hpp"
#include "diagnostics/module_wire.hpp"

namespace yw1::hooking {
constexpr std::uint64_t kSwapCallOffset = 0x2599F4, kSwapPltOffset = 0x65E6D0;
struct CallsiteTarget {
    game::ModuleImage main, subsdk0;
    std::uint64_t executableSize = 0;
    std::uintptr_t callback = 0, originalSlot = 0;
    diagnostics::CallbackThreadPolicy threadPolicy =
        diagnostics::CallbackThreadPolicy::Unverified;
};
struct CallsitePlan {
    std::uintptr_t target = 0, original = 0;
    std::uint32_t expected = 0, replacement = 0, originalBranch = 0;
};
Result prepareCallsite(const CallsiteTarget& target, CallsitePlan& plan);

// 元のBLと新しいBLだけを単一の32bit CASで交換する専用契約。
// 元処理を呼ぶBLを先にCache同期し、公開後のコードはprocess寿命まで保持する。
class CallsitePlatform {
  public:
    virtual Result validate(const CallsiteTarget& target) = 0;
    virtual Result readTarget(std::uint32_t& word) = 0;
    virtual Result prepareOriginal(const CallsitePlan& plan) = 0;
    virtual Result mapTarget() = 0;
    virtual Result exchange(std::uint32_t expected, std::uint32_t replacement, bool& changed) = 0;
    virtual Result synchronizeTarget() = 0;
    virtual Result unmapTarget() = 0;
    virtual void publish(const diagnostics::CallbackWire& wire) = 0;
  protected:
    ~CallsitePlatform() = default;
};

class CallsiteInstaller {
  public:
    Result install(const CallsiteTarget& target, CallsitePlatform& platform,
                   diagnostics::CallbackWire& report);
  private:
    std::atomic_flag busy_ = ATOMIC_FLAG_INIT;
    bool attempted_ = false;
};
} // 名前空間 yw1::hooking
