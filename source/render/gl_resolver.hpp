#pragma once
#include "code_digest.hpp"
#include "game/symbols/symbol_registry.hpp"

namespace yw1::render {
using GlProc = void (*)();
struct GlFunctionSpec {
    const char* name;
    std::uint64_t entry, slot, target;
    std::uint32_t entrySize, targetSize;
    CodeDigest entryDigest, targetDigest;
    bool required;
};
class GlMemory {
  public:
    virtual bool read(std::uint64_t address, std::span<std::byte> bytes, bool code) = 0;
    virtual GlProc lookup(const char* name) = 0;
  protected:
    ~GlMemory() = default;
};
enum class ResolveResult : std::uint32_t { Ready, Identity, Bounds, Entry, Table, Target, Missing };
struct ResolveStats {
    std::uint32_t attempted = 0, verified = 0, required = 0, rejected = 0, firstFailure = UINT32_MAX;
    ResolveResult result = ResolveResult::Missing;
    std::uint64_t verifiedMask = 0, rejectedMask = 0;
};
class GlResolver {
  public:
    ResolveStats resolve(const game::ModuleImage& module, std::span<const GlFunctionSpec> specs,
                         GlMemory& memory, std::span<GlProc> output);
    static constexpr unsigned kMaximumFunctions = 64;
};
} // 名前空間 yw1::render
