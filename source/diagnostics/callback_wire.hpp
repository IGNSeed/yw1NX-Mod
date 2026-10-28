#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace yw1::diagnostics {
constexpr std::uint64_t kCallbackMagic = 0x38424345315759;
constexpr std::uint64_t kCallbackAbi = 1, kCallbackBuildTag = 0xB0028;
enum class CallbackField : unsigned {
    Magic, Abi, Size, Profile, Method, Target, Original, InstallAttempted, InstallResult,
    GuardResult, OriginalPublished, HookActive, CallbackCount, OriginalCalls, OriginalReturns,
    LastOriginalResult, ThreadId, ThreadKnown, ReentryCount, HookError, RollbackAttempted,
    RollbackResult, InitializationStage, PatchAttempted, MappingResult, CacheResult,
    CleanupResult, ThreadPolicy, StubResult, ResourcesRetained, InFlight, BuildTag
};
constexpr unsigned kCallbackWords = 32;
enum class CallbackProfile : std::uint64_t { ReadOnly = 1, Canary = 2 };
enum class CallbackThreadPolicy : std::uint64_t { Unverified, AtomicDirectBl };
struct CallbackWire {
    std::array<std::uint64_t, kCallbackWords> words{};
    std::uint64_t& operator[](CallbackField field) { return words[static_cast<unsigned>(field)]; }
    std::uint64_t operator[](CallbackField field) const {
        return words[static_cast<unsigned>(field)];
    }
};
static_assert(sizeof(CallbackWire) == 256);
struct alignas(4096) CallbackPublication {
    std::atomic<std::uint64_t> generation{0};
    std::atomic<std::uint64_t> words[kCallbackWords]{};
};
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
extern "C" { extern CallbackPublication yw1_callback_diagnostics; }
CallbackWire initialCallbackWire(CallbackProfile profile);
bool validCallbackWire(const CallbackWire& wire);
void initializeCallbackWire(CallbackProfile profile);
void publishCallbackConfiguration(const CallbackWire& wire);
void recordCallbackEnter();
void recordCallbackReturn(std::uint32_t result);
const char* callbackActivity(const CallbackWire& wire);
const char* callbackReason(const CallbackWire& wire);
} // 名前空間 yw1::diagnostics
