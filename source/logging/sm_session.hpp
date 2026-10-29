#pragma once

#include "ipc_fs.hpp"
#include "game/symbols/symbol_registry.hpp"

namespace yw1::logging {

constexpr Result kSmIdentity = 0xE111;
constexpr Result kSmUnavailable = 0xE112;

// 実体は呼出し元が所有し、検証では読み取り以外を許可しない。
class SmSessionMemory {
  public:
    virtual bool read(std::uint64_t address, std::span<std::byte> bytes) = 0;
};

Result locateSmSession(const game::ModuleImage& sdk, SmSessionMemory& memory, Handle& session);
Result loadNativeSmSession(Handle& session);

} // 名前空間 yw1::logging
