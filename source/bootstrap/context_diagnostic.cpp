#include "context_diagnostic.hpp"
#include "diagnostics/context_bindings.hpp"
#include "diagnostics/context_probe.hpp"
#include "diagnostics/callback_wire.hpp"
#include "diagnostics/module_wire.hpp"
#include <common.hpp>
#include <lib/util/sys/mem_layout.hpp>

namespace yw1::bootstrap {
using namespace diagnostics;
void initializeContextDiagnostic() { publishContext(initialContextWire(YW1_CALLBACK_PROFILE)); }
#if YW1_CALLBACK_PROFILE == 2
namespace {
constexpr std::uint32_t kPointer = 0xE290;
bool readableRange(std::uintptr_t address, std::size_t length, unsigned permission) {
    if (!address || !length || address > UINTPTR_MAX - length) { return false; }
    MemoryInfo info{}; u32 page = 0;
    return svcQueryMemory(&info, &page, address) == 0 &&
        (info.perm & permission) == permission && info.attr == 0 &&
        contains(info.addr, info.size, address, length);
}
bool note(const exl::util::ModuleInfo& module, std::uint64_t offset, const game::BuildId& id) {
    if (module.m_Total.m_Start > UINTPTR_MAX - offset) { return false; }
    const auto address = module.m_Total.m_Start + offset;
    if (!contains(module.m_Rodata.m_Start, module.m_Rodata.m_Size, address, 36)) { return false; }
    constexpr std::uint8_t header[]{4, 0, 0, 0, 20, 0, 0, 0, 3, 0, 0, 0, 'G', 'N', 'U', 0};
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(address);
    for (unsigned i = 0; i < 16; ++i) { if (bytes[i] != header[i]) { return false; } }
    for (unsigned i = 0; i < 32; ++i) {
        if ((i < 20 ? bytes[i + 16] : 0) != id[i]) { return false; }
    }
    return true;
}
class NativeContextPlatform final : public ContextPlatform {
  public:
    void configure() {
        using exl::util::ModuleIndex;
        if (!exl::util::HasModule(ModuleIndex::Main) || !exl::util::HasModule(ModuleIndex::Sdk) ||
            !exl::util::HasModule(ModuleIndex::Subsdk0)) { return; }
        const auto& main = exl::util::GetModuleInfo(ModuleIndex::Main);
        const auto& sdk = exl::util::GetModuleInfo(ModuleIndex::Sdk);
        const auto& sub = exl::util::GetModuleInfo(ModuleIndex::Subsdk0);
        if (!note(main, 0x880184, std::to_array(kMainBuildId)) ||
            !note(sdk, 0xBDA02C, kContextSdkId) || !note(sub, 0x171BD88, kSubsdk0Id)) { return; }
        sub_ = {sub.m_Total.m_Start, sub.m_Total.m_Size, {kTitleId, "subsdk0", kSubsdk0Id}};
        sdk_ = {sdk.m_Total.m_Start, sdk.m_Total.m_Size, {kTitleId, "sdk", kContextSdkId}};
        text_[0] = {sub.m_Text.m_Start, sub.m_Text.m_Size};
        text_[1] = {sdk.m_Text.m_Start, sdk.m_Text.m_Size};
        for (unsigned i = 0; i < addresses_.size(); ++i) {
            const auto address = resolveContextBinding(i, i == 5 ? sdk_ : sub_);
            if (!address) { return; }
            addresses_[i] = *address;
        }
        api = {reinterpret_cast<decltype(api.queryApi)>(addresses_[0]),
               reinterpret_cast<decltype(api.context)>(addresses_[1]),
               reinterpret_cast<decltype(api.display)>(addresses_[2]),
               reinterpret_cast<decltype(api.surface)>(addresses_[3]),
               reinterpret_cast<decltype(api.getProc)>(addresses_[4])};
        configuration_ = 0;
    }
    std::uint32_t validate(const ContextApi& value) override {
        if (configuration_) { return configuration_; }
        if (value.queryApi != api.queryApi || value.context != api.context ||
            value.display != api.display || value.surface != api.surface ||
            value.getProc != api.getProc) { return kPointer; }
        for (unsigned i = 0; i < addresses_.size(); ++i) {
            if (!code(i, addresses_[i])) { return kPointer; }
        }
        return 0;
    }
    ProbeReason ready() override {
        // static pointerの所在地も対象版のSymbolRegistryを通して限定する。
        const game::VersionProfile profile{kTitleId, "1.4.0", "subsdk0", kSubsdk0Id};
        const game::SymbolRecord symbol{"egl dispatch manager", "subsdk0", kSubsdk0Id,
                                        0x1A45D60, 8, 8, game::EvidenceStatus::Verified};
        const auto matched = game::VersionManager(std::span(&profile, 1)).match(sub_.identity);
        const auto address = matched ? game::SymbolRegistry(std::span(&symbol, 1)).resolve(
            symbol.name, *matched, sub_) : std::nullopt;
        if (!address || !readableRange(*address, 8, Perm_R)) { return ProbeReason::TlsNotReady; }
        const auto manager = __atomic_load_n(reinterpret_cast<const std::uintptr_t*>(*address),
                                             __ATOMIC_ACQUIRE);
        if (!manager || manager % 8 || !readableRange(manager, 0x50, Perm_Rw)) {
            return ProbeReason::TlsNotReady;
        }
        const auto key = __atomic_load_n(reinterpret_cast<const std::uint32_t*>(manager + 0x48),
                                         __ATOMIC_ACQUIRE);
        // SDK GetTlsValueの5命令を照合し、その実読取範囲を先に検査する。
        // 上限64は採用範囲を狭めるための制限で、SDK全Slot数の断定ではない。
        if (key >= 64) { return ProbeReason::TlsNotReady; }
        std::uintptr_t tlsBase = 0;
        asm volatile("mrs %0, tpidrro_el0" : "=r"(tlsBase));
        if (!readableRange(tlsBase, 0x200, Perm_R)) { return ProbeReason::TlsNotReady; }
        const auto thread = __atomic_load_n(
            reinterpret_cast<const std::uintptr_t*>(tlsBase + 0x1F8), __ATOMIC_ACQUIRE);
        if (!thread || thread > UINTPTR_MAX - 0x80 - key * 8 ||
            !readableRange(thread + 0x80 + key * 8, 8, Perm_R)) {
            return ProbeReason::TlsNotReady;
        }
        const auto getTls = reinterpret_cast<std::uintptr_t (*)(std::uint32_t)>(addresses_[5]);
        const auto state = getTls(key);
        if (!state || state % 8 || !readableRange(state, 0x2C0, Perm_Rw)) {
            return ProbeReason::TlsNotReady;
        }
        constexpr std::uint64_t slots[]{0xC0, 0xC8, 0xD0, 0xE8, 0x100};
        constexpr std::uint64_t expected[]{0xE1F0, 0xD6C0, 0xD6F0, 0xE2F0, 0xE380};
        for (unsigned i = 0; i < 5; ++i) {
            const auto function = __atomic_load_n(
                reinterpret_cast<const std::uintptr_t*>(state + slots[i]), __ATOMIC_ACQUIRE);
            if (function != sub_.base + expected[i]) {
                // 別modeのQueryAPIはerrorを3000へstoreするため、一切呼ばない。
                return ProbeReason::UnsupportedDispatch;
            }
        }
        return ProbeReason::None;
    }
    std::uint32_t threadId(std::uint64_t& value) override {
        u64 native = 0;
        const auto rc = svcGetThreadId(&native, CUR_THREAD_HANDLE);
        if (rc == 0) { value = native; }
        return rc;
    }
    bool glPointer(GenericProc pointer, bool stringFunction) override {
        const auto index = stringFunction ? 6u : 7u;
        return pointer && code(index, reinterpret_cast<std::uintptr_t>(pointer));
    }
    bool readable(std::uintptr_t address, std::size_t size) override {
        return readableRange(address, size, Perm_R);
    }
    void publish(const ContextWire& wire) override { publishContext(wire); }
    ContextApi api{};
  private:
    bool code(unsigned index, std::uintptr_t address) {
        const auto& binding = kContextBindings[index];
        const auto& range = text_[index == 5 ? 1 : 0];
        if (address != addresses_[index] ||
            !contains(range[0], range[1], address, binding.symbol.size) ||
            !readableRange(address, binding.symbol.size, Perm_Rx)) { return false; }
        const auto* words = reinterpret_cast<const std::uint32_t*>(address);
        for (unsigned i = 0; i < binding.symbol.size / 4; ++i) {
            if (__atomic_load_n(words + i, __ATOMIC_ACQUIRE) != binding.instructions[i]) {
                return false;
            }
        }
        return true;
    }
    std::uint32_t configuration_ = kContextIdentityError;
    game::ModuleImage sub_{}, sdk_{};
    std::array<std::array<std::uint64_t, 2>, 2> text_{};
    std::array<std::uintptr_t, 8> addresses_{};
};
constinit NativeContextPlatform platform;
constinit ContextProbe probe;
} // 無名名前空間
void prepareContextDiagnostic() {
    platform.configure();
    probe.initialize(2, platform.api, platform);
}
void probeContextCallback() {
    using F = CallbackField;
    const auto word = [](F f) {
        return yw1_callback_diagnostics.words[static_cast<unsigned>(f)].load(
            std::memory_order_acquire);
    };
    probe.onCallback(word(F::CallbackCount), word(F::HookActive) == 1 &&
        word(F::OriginalPublished) == 1 && word(F::InstallResult) == 0 && word(F::GuardResult) == 0);
}
#else
void prepareContextDiagnostic() {}
void probeContextCallback() {}
#endif
} // 名前空間 yw1::bootstrap
