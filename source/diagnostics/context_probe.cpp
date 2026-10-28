#include "context_probe.hpp"
#include <limits>

namespace yw1::diagnostics {
namespace {
using F = ContextField;
using I = ContextItem;
constexpr auto kUnwritten = std::numeric_limits<std::int32_t>::min();
constexpr std::uint32_t kDesktop = 0x30A2, kEs = 0x30A0;
template <std::size_t N>
ValueState copyText(std::array<char, N>& output, const unsigned char* source,
                    ContextPlatform& platform) {
    output.fill(0);
    if (source == nullptr) { return ValueState::Failed; }
    const auto start = reinterpret_cast<std::uintptr_t>(source);
    if (start > UINTPTR_MAX - N) { return ValueState::Failed; }
    std::size_t checked = 0;
    for (std::size_t i = 0; i < N; ++i) {
        if (i == checked) {
            const auto pageRemaining = 4096 - (start + i) % 4096;
            const auto length = pageRemaining < N - i ? pageRemaining : N - i;
            if (!platform.readable(start + i, length)) { return ValueState::Failed; }
            checked += length;
        }
        const auto c = source[i];
        if (c == 0) { return i == 0 ? ValueState::Failed : ValueState::Observed; }
        if (i + 1 == N) { return ValueState::Truncated; }
        output[i] = static_cast<char>(c);
    }
    return ValueState::Truncated;
}
bool number(const char*& p, std::int32_t& n) {
    unsigned digits = 0;
    while (*p >= '0' && *p <= '9' && digits < 2) { n = n * 10 + *p++ - '0'; ++digits; }
    return digits && !(*p >= '0' && *p <= '9');
}
bool version(const ContextWire& wire, std::int32_t& major, std::int32_t& minor) {
    const char* p = wire.version.data();
    const bool es = wire[F::CurrentApi] == kEs;
    if (es) {
        constexpr char prefix[] = "OpenGL ES ";
        for (unsigned i = 0; i < sizeof(prefix) - 1; ++i) {
            if (*p++ != prefix[i]) { return false; }
        }
    }
    if (!number(p, major) || *p++ != '.' || !number(p, minor) ||
        (*p != 0 && *p != ' ' && *p != '.')) { return false; }
    if (es) { return (major == 2 && minor == 0) || (major == 3 && minor <= 2); }
    return (major == 1 && minor <= 5) || (major == 2 && minor <= 1) ||
           (major == 3 && minor <= 3) || (major == 4 && minor <= 6);
}
} // 無名名前空間
void ContextProbe::initialize(unsigned profile, const ContextApi& api, ContextPlatform& platform) {
    if (busy_.test_and_set(std::memory_order_acquire)) { return; }
    if (!initialized_.load(std::memory_order_relaxed)) {
        api_ = api; platform_ = &platform; wire_ = initialContextWire(profile);
        if (profile == 1) { terminal_.store(true, std::memory_order_relaxed); }
        platform_->publish(wire_);
        initialized_.store(true, std::memory_order_release);
    }
    busy_.clear(std::memory_order_release);
}
void ContextProbe::stop(ProbeReason reason) {
    wire_[F::Reason] = static_cast<unsigned>(reason);
    wire_[F::Status] = static_cast<unsigned>(ProbeStatus::Failed);
    terminal_.store(true, std::memory_order_release);
}
void ContextProbe::onCallback(std::uint64_t count, bool hookReady) {
    if (!hookReady || !initialized_.load(std::memory_order_acquire) ||
        terminal_.load(std::memory_order_acquire) || count < next_.load(std::memory_order_relaxed) ||
        busy_.test_and_set(std::memory_order_acquire)) { return; }
    if (!terminal_.load(std::memory_order_relaxed) && count >= next_.load()) {
        capture(count);
        platform_->publish(wire_);
    }
    busy_.clear(std::memory_order_release);
}
bool ContextProbe::sameBinding() {
    const auto invalidate = [&] {
        wire_[F::BindingValid] = 0;
        for (unsigned i = static_cast<unsigned>(I::Version); i < 15; ++i) {
            if (wire_.states[i] == ValueState::Observed ||
                wire_.states[i] == ValueState::Truncated) { wire_.states[i] = ValueState::Failed; }
        }
    };
    const auto ready = platform_->ready();
    if (ready != ProbeReason::None) { invalidate(); stop(ready); return false; }
    std::uint64_t thread = 0;
    wire_[F::ThreadResult] = platform_->threadId(thread);
    if (wire_[F::ThreadResult] != 0) {
        wire_.state(I::Thread) = ValueState::Failed;
        invalidate(); stop(ProbeReason::ThreadUnavailable); return false;
    }
    const bool same = thread == wire_[F::ThreadId] &&
        api_.queryApi() == wire_[F::CurrentApi] &&
        reinterpret_cast<std::uintptr_t>(api_.context()) == wire_[F::Context] &&
        reinterpret_cast<std::uintptr_t>(api_.display()) == wire_[F::Display] &&
        reinterpret_cast<std::uintptr_t>(api_.surface(0x3059)) == wire_[F::DrawSurface] &&
        reinterpret_cast<std::uintptr_t>(api_.surface(0x305A)) == wire_[F::ReadSurface];
    if (!same) {
        wire_[F::BindingChanged] = 1; invalidate();
        stop(ProbeReason::BindingChanged);
    }
    return same;
}
void ContextProbe::capture(std::uint64_t count) {
    ++wire_[F::Attempts]; wire_[F::Attempted] = 1; wire_[F::SampleCallback] = count;
    wire_[F::GuardResult] = platform_->validate(api_);
    if (!api_.queryApi || !api_.context || !api_.display || !api_.surface || !api_.getProc) {
        if (wire_[F::GuardResult] == 0) { wire_[F::GuardResult] = 0xE290; }
    }
    if (wire_[F::GuardResult] != 0) {
        stop(wire_[F::GuardResult] == kContextIdentityError ? ProbeReason::Identity
                                                         : ProbeReason::InvalidPointer);
        return;
    }
    std::uint64_t thread = 0;
    wire_[F::ThreadResult] = platform_->threadId(thread);
    if (wire_[F::ThreadResult] != 0 || thread == 0) {
        wire_.state(I::Thread) = ValueState::Failed;
        stop(ProbeReason::ThreadUnavailable); return;
    }
    wire_.state(I::Thread) = ValueState::Observed; wire_[F::ThreadId] = thread;
    if (owner_ && owner_ != thread) { stop(ProbeReason::DifferentThread); return; }
    owner_ = thread;
    const auto ready = platform_->ready();
    if (ready != ProbeReason::None) {
        if (ready == ProbeReason::TlsNotReady && wire_[F::Attempts] < 3 && count <= UINT64_MAX - 120) {
            wire_[F::Status] = static_cast<unsigned>(ProbeStatus::WaitingContext);
            wire_[F::Reason] = static_cast<unsigned>(ready);
            wire_[F::RetryAt] = count + 120; next_.store(count + 120); return;
        }
        stop(ready); return;
    }
    wire_[F::ApiValidatedMask] = 31;
    wire_[F::CurrentApi] = api_.queryApi(); wire_.state(I::Api) = ValueState::Observed;
    wire_[F::Context] = reinterpret_cast<std::uintptr_t>(api_.context());
    wire_.state(I::Context) = ValueState::Observed;
    if (!wire_[F::Context]) {
        if (wire_[F::Attempts] < 3 && count <= UINT64_MAX - 120) {
            wire_[F::Status] = static_cast<unsigned>(ProbeStatus::WaitingContext);
            wire_[F::Reason] = static_cast<unsigned>(ProbeReason::NoContext);
            wire_[F::RetryAt] = count + 120; next_.store(count + 120); return;
        }
        stop(ProbeReason::NoContext); return;
    }
    wire_[F::Display] = reinterpret_cast<std::uintptr_t>(api_.display());
    wire_[F::DrawSurface] = reinterpret_cast<std::uintptr_t>(api_.surface(0x3059));
    wire_[F::ReadSurface] = reinterpret_cast<std::uintptr_t>(api_.surface(0x305A));
    for (auto i : {I::Display, I::DrawSurface, I::ReadSurface}) {
        wire_.state(i) = ValueState::Observed;
    }
    if (!wire_[F::Display] || !wire_[F::DrawSurface] || !wire_[F::ReadSurface]) {
        stop(ProbeReason::InvalidBinding); return;
    }
    wire_[F::BindingValid] = 1;
    if (wire_[F::CurrentApi] != kDesktop && wire_[F::CurrentApi] != kEs) {
        for (unsigned i = static_cast<unsigned>(I::Version); i < 15; ++i) {
            wire_.states[i] = ValueState::Unsupported;
        }
        stop(ProbeReason::UnsupportedApi); return;
    }
    const auto stringProc = api_.getProc("glGetString");
    const auto integerProc = api_.getProc("glGetIntegerv");
    wire_[F::GlStringPointer] = reinterpret_cast<std::uintptr_t>(stringProc);
    wire_[F::GlIntegerPointer] = reinterpret_cast<std::uintptr_t>(integerProc);
    const bool stringValid = stringProc && platform_->glPointer(stringProc, true);
    const bool integerValid = integerProc && platform_->glPointer(integerProc, false);
    wire_[F::PointerResult] = stringValid && integerValid ? 0 : 0xE290;
    if (!stringValid || !integerValid) { stop(ProbeReason::GlFunctionUnavailable); return; }
    wire_[F::ApiValidatedMask] = 127;
    const auto getString = reinterpret_cast<GlGetString>(stringProc);
    const auto getInteger = reinterpret_cast<GlGetInteger>(integerProc);
    wire_.state(I::Version) = copyText(wire_.version, getString(0x1F02), *platform_);
    std::int32_t major = 0, minor = 0;
    if (wire_.state(I::Version) != ValueState::Observed || !version(wire_, major, minor)) {
        stop(ProbeReason::InvalidVersion); return;
    }
    wire_.state(I::Vendor) = copyText(wire_.vendor, getString(0x1F00), *platform_);
    wire_.state(I::Renderer) = copyText(wire_.renderer, getString(0x1F01), *platform_);
    const auto query = [&](std::uint32_t name, unsigned offset, unsigned length, I item) {
        for (unsigned i = 0; i < length; ++i) { wire_.integers[offset + i] = kUnwritten; }
        getInteger(name, wire_.integers.data() + offset);
        bool written = true;
        for (unsigned i = 0; i < length; ++i) {
            written &= wire_.integers[offset + i] != kUnwritten;
        }
        wire_.state(item) = written ? ValueState::Observed : ValueState::Failed;
    };
    query(0x0BA2, 0, 4, I::Viewport);
    if (wire_.integers[2] < 0 || wire_.integers[3] < 0) {
        wire_.state(I::Viewport) = ValueState::Failed;
    }
    if (major >= 2) {
        wire_.state(I::Glsl) = copyText(wire_.glsl, getString(0x8B8C), *platform_);
        query(0x8B8D, 5, 1, I::Program);
    } else { wire_.state(I::Glsl) = wire_.state(I::Program) = ValueState::Unsupported; }
    if (major >= 3 || wire_[F::CurrentApi] == kEs) { query(0x8CA6, 4, 1, I::Framebuffer); }
    else { wire_.state(I::Framebuffer) = ValueState::Unsupported; }
    if (major >= 3) {
        query(0x821B, 6, 1, I::VersionIntegers);
        const auto first = wire_.state(I::VersionIntegers);
        query(0x821C, 7, 1, I::VersionIntegers);
        if (first != ValueState::Observed || wire_.integers[6] != major ||
            wire_.integers[7] != minor) { wire_.state(I::VersionIntegers) = ValueState::Failed; }
    } else { wire_.state(I::VersionIntegers) = ValueState::Unsupported; }
    if (wire_[F::CurrentApi] == kDesktop && (major > 3 || (major == 3 && minor >= 2))) {
        query(0x9126, 8, 1, I::Profile);
    } else { wire_.state(I::Profile) = ValueState::Unsupported; }
    if (!sameBinding()) { return; }
    for (unsigned i = static_cast<unsigned>(I::Version); i < 15; ++i) {
        if (wire_.states[i] == ValueState::Failed) { stop(ProbeReason::QueryUnwritten); return; }
    }
    wire_[F::Reason] = 0; wire_[F::Completed] = 1;
    wire_[F::Status] = static_cast<unsigned>(ProbeStatus::Captured);
    terminal_.store(true, std::memory_order_release);
}
} // 名前空間 yw1::diagnostics
