#pragma once
#include "context_wire.hpp"
#include "egl_snapshot.hpp"

namespace yw1::diagnostics {
constexpr std::uint32_t kContextIdentityError = 0xE291;
using GlGetString = const unsigned char* (*)(std::uint32_t);
using GlGetInteger = void (*)(std::uint32_t, std::int32_t*);
struct ContextApi {
    std::uint32_t (*queryApi)() = nullptr;
    EglHandle (*context)() = nullptr;
    EglHandle (*display)() = nullptr;
    EglHandle (*surface)(std::int32_t) = nullptr;
    GenericProc (*getProc)(const char*) = nullptr;
};
class ContextPlatform {
  public:
    virtual std::uint32_t validate(const ContextApi&) = 0;
    virtual ProbeReason ready() = 0;
    virtual std::uint32_t threadId(std::uint64_t&) = 0;
    virtual bool glPointer(GenericProc pointer, bool stringFunction) = 0;
    virtual bool readable(std::uintptr_t address, std::size_t size) = 0;
    virtual void publish(const ContextWire& wire) = 0;
  protected:
    // 所有者が静的に保持する参照だけを使い、delete経路を作らない。
    ~ContextPlatform() = default;
};
class ContextProbe {
  public:
    void initialize(unsigned profile, const ContextApi& api, ContextPlatform& platform);
    void onCallback(std::uint64_t count, bool hookReady);
  private:
    void capture(std::uint64_t count);
    void stop(ProbeReason reason);
    bool sameBinding();
    ContextWire wire_{};
    ContextApi api_{};
    ContextPlatform* platform_ = nullptr;
    std::atomic<bool> initialized_{false}, terminal_{false};
    std::atomic_flag busy_ = ATOMIC_FLAG_INIT;
    std::atomic<std::uint64_t> next_{1};
    std::uint64_t owner_ = 0;
};
} // 名前空間 yw1::diagnostics
