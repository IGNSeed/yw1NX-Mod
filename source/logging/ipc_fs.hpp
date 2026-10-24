#pragma once

#include "memory_log.hpp"
#include "progress.hpp"

namespace yw1::logging {

using Handle = std::uint32_t;
constexpr std::size_t kPageSize = 4096;
constexpr std::size_t kFsPathSize = 0x301;
constexpr std::uint64_t kFileCapacity = 1024 * 1024;
constexpr char kLogPath[] = "/atmosphere/contents/0100C0000CEEA000/yw1NX-MOD/logs.txt";
constexpr Result kPathNotFound = (1 << 9) | 2;
constexpr Result kPathAlreadyExists = (2 << 9) | 2;

// 実装はraw SVCだけを使用する。ホスト試験では同じ境界へ失敗を注入する。
struct KernelApi {
    void* user;
    Result (*connect)(void*, Handle*, const char*);
    Result (*submit)(void*, Handle*, void*, std::size_t, Handle);
    Result (*wait)(void*, Handle, std::uint64_t);
    Result (*close)(void*, Handle);
    std::uint64_t (*ticks)(void*);
    void (*sleep)(void*, std::uint64_t) = nullptr;
};

struct IpcStorage {
    // 非同期IPC中、packet全ページはユーザーモードからアクセスできない。
    alignas(kPageSize) std::uint32_t packet[kPageSize / 4]{};
    alignas(kPageSize) char path[kPageSize]{};
    alignas(kPageSize) char receipt[kPageSize]{};
};

// 単一Workerが所有する非domainのSM/FSP/FS/Fileセッション。
class IpcFs {
  public:
    IpcFs(KernelApi api, IpcStorage& storage, std::uint64_t deadlineTicks)
        : api_(api), storage_(storage), deadline_(deadlineTicks) {}
    Result initialize();
    Result openSd();
    Result entryType(const char* path, std::uint32_t& type);
    Result createDirectory(const char* path);
    Result createFile(const char* path);
    Result openFile(const char* path);
    Result fileSize(std::uint64_t& size);
    Result write(std::uint64_t offset, const char* data, std::size_t size);
    Result flush();
    Result closeAll();
    bool quarantined() const {
        return quarantined_;
    }
    std::uint32_t requests() const {
        return requests_;
    }

  private:
    Result callRaw(Handle session, std::uint32_t command, const void* input, std::size_t inputSize,
                   void* output, std::size_t outputSize, Handle* object, const char* path,
                   const char* buffer, std::size_t bufferSize, bool pid);
    Result call(Handle session, std::uint32_t command, const void* input, std::size_t inputSize,
                void* output = nullptr, std::size_t outputSize = 0, Handle* object = nullptr,
                const char* path = nullptr, const char* buffer = nullptr,
                std::size_t bufferSize = 0, bool pid = false);
    Result release(Handle& handle);
    Result detachSm();
    KernelApi api_;
    IpcStorage& storage_;
    std::uint64_t deadline_;
    Handle sm_ = 0;
    Handle proxy_ = 0;
    Handle fs_ = 0;
    Handle file_ = 0;
    std::uint32_t requests_ = 0;
    bool quarantined_ = false;
    bool smNeedsDetach_ = false;
};

} // 名前空間 yw1::logging
