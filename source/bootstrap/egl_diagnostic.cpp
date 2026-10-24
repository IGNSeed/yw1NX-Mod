#include "diagnostics/egl_wire.hpp"
#include "logging/memory_log.hpp"
#include <common.hpp>
#include <lib/util/sys/mem_layout.hpp>

namespace yw1::bootstrap {
namespace {
using namespace diagnostics;
bool checkNote(const exl::util::ModuleInfo& module, std::uint64_t offset, const game::BuildId& id) {
    const auto start = module.m_Rodata.m_Start;
    const auto address = module.m_Total.m_Start + offset;
    if (address < start || !contains(start, module.m_Rodata.m_Size, address, 36)) {
        return false;
    }
    constexpr std::uint8_t header[]{4, 0, 0, 0, 20, 0, 0, 0, 3, 0, 0, 0, 'G', 'N', 'U', 0};
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(address);
    for (unsigned i = 0; i < 16; ++i) {
        if (bytes[i] != header[i]) {
            return false;
        }
    }
    for (unsigned i = 0; i < 32; ++i) {
        // GNU descriptorは20bytes。NSOの残る12bytesがzeroである対象だけを扱う。
        if ((i < 20 ? bytes[16 + i] : 0) != id[i]) {
            return false;
        }
    }
    return true;
}
} // 無名名前空間
void runEglDiagnostic() {
    using namespace diagnostics;
    EglDiagnosticWire state;
    state.guardStatus = static_cast<std::uint32_t>(HookGuardStatus::WrongIdentity);
    constexpr game::BuildId mainId{0x7F, 0x35, 0xBD, 0xFC, 0x5D, 0xE4, 0x6C, 0xF1, 0xED, 0xE9,
                                   0xC0, 0x61, 0x8B, 0x47, 0x04, 0x7B, 0xAF, 0xF4, 0x9E, 0x12};
    constexpr game::BuildId sdkId{0x86, 0xA2, 0xAF, 0xD8, 0x63, 0xC9, 0x90, 0x62, 0xD6, 0x66,
                                  0x22, 0x6A, 0x9E, 0x7A, 0xA7, 0x64, 0xB6, 0x53, 0xEC, 0xEC};
    using exl::util::ModuleIndex;
    if (exl::util::HasModule(ModuleIndex::Main) && exl::util::HasModule(ModuleIndex::Subsdk0) &&
        exl::util::HasModule(ModuleIndex::Sdk)) {
        const auto& main = exl::util::GetModuleInfo(ModuleIndex::Main);
        const auto& sdk = exl::util::GetModuleInfo(ModuleIndex::Sdk);
        const auto& sub = exl::util::GetModuleInfo(ModuleIndex::Subsdk0);
        if (checkNote(main, 0x880184, mainId) && checkNote(sdk, 0xBDA02C, sdkId) &&
            checkNote(sub, 0x171BD88, kSubsdk0Id)) {
            game::ModuleImage image{
                sub.m_Total.m_Start, sub.m_Total.m_Size, {kTitleId, "subsdk0", kSubsdk0Id}};
            if (contains(sub.m_Text.m_Start, sub.m_Text.m_Size, image.base + 0x10B60, 20)) {
                const auto* code = reinterpret_cast<const std::uint32_t*>(image.base + 0x10B60);
                state.guardStatus = static_cast<std::uint32_t>(checkSwap(
                    image, 0x10B60, sub.m_Text.m_Size, std::span<const std::uint32_t>(code, 5)));
            } else {
                state.guardStatus = static_cast<std::uint32_t>(HookGuardStatus::InvalidRange);
            }
        }
    }
    // Original公開順・失敗時abort・実行中patchの条件が未解決なので登録を試行しない。
    state.blockedReason = 1;
#if YW1_BOOTSTRAP_SAFE_HOOK_DIAGNOSTIC
    // 実行したGuardだけを転記する。未接続のplatform/JIT/leaseを成功扱いしない。
    auto& installer = state.installer;
    using hooking::error;
    using hooking::HookError;
    const auto guard = static_cast<HookGuardStatus>(state.guardStatus);
    installer.identity = guard == HookGuardStatus::WrongIdentity ? error(HookError::Identity) : 0;
    if (installer.identity == 0) {
        installer.instructions =
            guard == HookGuardStatus::Passed
                ? 0
                : error(guard == HookGuardStatus::InvalidInstructions ? HookError::Instructions
                                                                      : HookError::Boundary);
    }
    installer.preflight = installer.identity != 0 ? installer.identity : installer.instructions;
    installer.state =
        installer.preflight == 0 ? hooking::HookState::PreflightPassed : hooking::HookState::Failed;
    installer.blockedReason = error(HookError::UnverifiedThread);
#endif
    publishEglDiagnostic(state);
    constexpr char kMessage[] = "[YW1] egl_hook_enabled=0; installer_safety_unverified\n";
    static_cast<void>(logging::memoryLog().append(kMessage, sizeof(kMessage) - 1));
    constexpr char kLabel[] = "[YW1] egl_hook_guard_status=";
    static_cast<void>(
        logging::memoryLog().appendResult(kLabel, sizeof(kLabel) - 1, state.guardStatus));
}
} // 名前空間 yw1::bootstrap
