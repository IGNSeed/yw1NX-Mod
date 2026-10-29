#include "sm_session.hpp"
#include "diagnostics/context_bindings.hpp"
#include <bit>

namespace yw1::logging {
namespace {
using namespace game;
constexpr auto kSdkId = diagnostics::kContextSdkId;
constexpr VersionProfile kProfile{0x0100C0000CEEA000, "1.4.0", "sdk", kSdkId};
constexpr SymbolRecord kSymbols[]{
    {"SM interface owner", "sdk", kSdkId, 0xC46AC8, 8, 8, EvidenceStatus::Verified},
    {"SM interface storage", "sdk", kSdkId, 0xC46AD0, 8, 8, EvidenceStatus::Verified},
    {"SM interface vtable", "sdk", kSdkId, 0xBE0618, 72, 8, EvidenceStatus::Verified},
    {"SM interface construction", "sdk", kSdkId, 0xD97D4, 36, 4, EvidenceStatus::Verified},
    {"SM Finalize no-op", "sdk", kSdkId, 0xD9880, 8, 4, EvidenceStatus::Verified},
    {"SM GetService forwarding", "sdk", kSdkId, 0xD9D30, 32, 4, EvidenceStatus::Verified},
};
constexpr std::uint32_t kConstruction[]{0x320003E8, 0xB9001A88, 0x79001288,
    0xF0005828, 0x91186108, 0xF9000288, 0x91016108, 0xB9000E97, 0xF9000A88};
constexpr std::uint32_t kFinalize[]{0xAA1F03E0, 0xD65F03C0};
constexpr std::uint32_t kForwarding[]{0xA9BF7BFD, 0x910003FD, 0x91002000, 0x320003E3,
    0x94113564, 0x92407C00, 0xA8C17BFD, 0xD65F03C0};

template <typename Value> bool read(SmSessionMemory& memory, std::uint64_t address, Value& out) {
    return memory.read(address, std::as_writable_bytes(std::span(&out, 1)));
}
template <std::size_t Count>
bool code(SmSessionMemory& memory, std::uint64_t address,
          const std::uint32_t (&expected)[Count]) {
    std::array<std::uint32_t, Count> actual{};
    if (!memory.read(address, std::as_writable_bytes(std::span(actual)))) { return false; }
    for (unsigned i = 0; i < Count; ++i) { if (actual[i] != expected[i]) { return false; } }
    return true;
}
} // 無名名前空間

Result locateSmSession(const game::ModuleImage& sdk, SmSessionMemory& memory, Handle& session) {
    session = 0;
    const auto version = VersionManager(std::span(&kProfile, 1)).match(sdk.identity);
    if (!version) { return kSmIdentity; }
    SymbolRegistry registry(kSymbols);
    std::array<std::uint64_t, std::size(kSymbols)> addresses{};
    for (unsigned i = 0; i < addresses.size(); ++i) {
        const auto value = registry.resolve(kSymbols[i].name, *version, sdk);
        if (!value) { return kSmIdentity; }
        addresses[i] = *value;
    }
    if (!code(memory, addresses[3], kConstruction) || !code(memory, addresses[4], kFinalize) ||
        !code(memory, addresses[5], kForwarding)) { return kSmIdentity; }
    std::uint64_t owner = 0, object = 0, vtable = 0, base = 0, method = 0;
    if (!read(memory, addresses[0], owner) || owner != addresses[1] ||
        !read(memory, owner, object) || !object || object % 8 || object > UINT64_MAX - 0x20 ||
        !read(memory, object, vtable) || vtable != addresses[2] ||
        !read(memory, vtable + 0x28, method) || method != addresses[5] ||
        !read(memory, object + 8, base)) { return kSmUnavailable; }
    // SDK ctorが公開する非domain proxyのflagsとnative handleだけを読み取る。
    const auto handle = static_cast<Handle>(base >> 32);
    if ((base & 0xFFFF) != 1 || !handle || (handle & 0xC0000000u)) {
        return kSmUnavailable;
    }
    std::uint64_t current = 0, currentBase = 0;
    if (!read(memory, addresses[1], current) || current != object ||
        !read(memory, object + 8, currentBase) || currentBase != base) { return kSmUnavailable; }
    session = handle;
    return 0;
}
} // 名前空間 yw1::logging
