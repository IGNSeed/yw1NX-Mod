#include "ipc_fs.hpp"

namespace yw1::logging {
namespace {
constexpr std::uint64_t kTickFrequency = 19200000;
constexpr std::uint64_t kWaitLimitNs = 1000000000;

void copyBytes(void* destination, const void* source, std::size_t count) {
    auto* out = static_cast<unsigned char*>(destination);
    const auto* in = static_cast<const unsigned char*>(source);
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = in[i];
    }
}

std::size_t align16(std::size_t offset) {
    return (offset + 15) & ~std::size_t{15};
}
}

Result IpcFs::call(Handle session, std::uint32_t command, const void* input, std::size_t inputSize,
                   void* output, std::size_t outputSize, Handle* object, const char* path,
                   const char* buffer, std::size_t bufferSize, bool pid,
                   char* receiveBuffer, std::size_t receiveSize) {
    Stage stage = Stage::SessionClose;
    if (session == sm_ || session == borrowedSm_) {
        stage = command == 0 ? Stage::SmRegister
                             : (command == 1 ? Stage::SmGetService : Stage::SmDetach);
    } else if (session == proxy_) {
        stage = command == 1 ? Stage::FspSetProcess : Stage::SdOpen;
    } else if (session == fs_) {
        switch (command) {
        case 0:
            stage = Stage::CreateFile;
            break;
        case 2:
            stage = Stage::CreateDirectory;
            break;
        case 7:
            stage = Stage::EntryType;
            break;
        case 8:
            stage = Stage::FileOpen;
            break;
        }
    } else if (session == file_) {
        stage = command == 4 ? Stage::FileSize : command == 0 ? Stage::FileRead :
            command == 1 ? Stage::FileWrite : Stage::FileFlush;
    }
    recordProgress(stage, kInProgress);
    return recordProgress(stage, callRaw(session, command, input, inputSize, output, outputSize,
                                         object, path, buffer, bufferSize, pid,
                                         receiveBuffer, receiveSize));
}

Result IpcFs::callRaw(Handle session, std::uint32_t command, const void* input,
                      std::size_t inputSize, void* output, std::size_t outputSize, Handle* object,
                      const char* path, const char* buffer, std::size_t bufferSize, bool pid,
                      char* receiveBuffer, std::size_t receiveSize) {
    // 送信失敗・タイムアウト時の再送はしない。書込みの二重実行を避ける。
    if (quarantined_ || session == 0 || inputSize > 64 || outputSize > 8 ||
        (inputSize && input == nullptr) || (outputSize && output == nullptr) ||
        bufferSize > kLogCapacity || (bufferSize && buffer == nullptr) || requests_ >= 64 ||
        receiveSize > kLogCapacity || (receiveSize && receiveBuffer == nullptr) ||
        (receiveSize && bufferSize)) {
        return kInvalidResponse;
    }
    const auto now = api_.ticks(api_.user);
    if (now >= deadline_) {
        return kDeadline;
    }
    auto* words = storage_.packet;
    for (std::size_t i = 0; i < kPageSize / 4; ++i) {
        words[i] = 0;
    }
    const std::uint32_t rawWords = static_cast<std::uint32_t>((32 + inputSize + 3) / 4);
    words[0] = 4 | (path ? 1u << 16 : 0) | (bufferSize ? 1u << 20 : 0) |
               (receiveSize ? 1u << 24 : 0);
    words[1] = rawWords | (pid ? 1u << 31 : 0);
    std::size_t cursor = 2;
    if (pid) {
        words[cursor++] = 1;
        cursor += 2;
    }
    if (path) {
        std::size_t length = 0;
        for (; length < kFsPathSize && path[length] != '\0'; ++length) {
        }
        if (length == kFsPathSize) {
            return kInvalidResponse;
        }
        for (std::size_t i = 0; i < kFsPathSize; ++i) {
            storage_.path[i] = i < length ? path[i] : '\0';
        }
        const auto address = reinterpret_cast<std::uintptr_t>(storage_.path);
        if (address >> 42) {
            return kInvalidResponse;
        }
        words[cursor++] = static_cast<std::uint32_t>(((address >> 36) & 63) << 6) |
                          static_cast<std::uint32_t>(((address >> 32) & 15) << 12) |
                          (static_cast<std::uint32_t>(kFsPathSize) << 16);
        words[cursor++] = static_cast<std::uint32_t>(address);
    }
    if (bufferSize) {
        const auto address = reinterpret_cast<std::uintptr_t>(buffer);
        // NonSecure MapAliasはlibnxのfsFileWriteと同じ契約。
        words[cursor++] = static_cast<std::uint32_t>(bufferSize);
        words[cursor++] = static_cast<std::uint32_t>(address);
        words[cursor++] = 1 | static_cast<std::uint32_t>(((address >> 36) & 0x3FFFFF) << 2) |
                          static_cast<std::uint32_t>(((address >> 32) & 15) << 28);
    }
    if (receiveSize) {
        const auto address = reinterpret_cast<std::uintptr_t>(receiveBuffer);
        if (address >> 58) { return kInvalidResponse; }
        words[cursor++] = static_cast<std::uint32_t>(receiveSize);
        words[cursor++] = static_cast<std::uint32_t>(address);
        words[cursor++] = 1 | static_cast<std::uint32_t>(((address >> 36) & 0x3FFFFF) << 2) |
                          static_cast<std::uint32_t>(((address >> 32) & 15) << 28);
    }
    const auto start = align16(cursor * 4) / 4;
    words[start] = 0x49434653;
    words[start + 2] = command;
    copyBytes(words + start + 4, input, inputSize);
    ++requests_;
    Result result = 0, closeResult = 0;
    if (api_.sendSync != nullptr) {
        result = api_.sendSync(api_.user, words, kPageSize, session);
        if (api_.abandoned != nullptr && api_.abandoned(api_.user)) {
            quarantined_ = true;
            return kDeadline;
        }
        if (result != 0) { return result; }
    } else {
        Handle event = 0;
        result = api_.submit(api_.user, &event, words, kPageSize, session);
        if (result != 0) {
            // SVC失敗時の出力handleは未定義なので閉じない。
            return result;
        }
        if (event == 0) {
            quarantined_ = true;
            return kInvalidResponse;
        }
        const auto submittedAt = api_.ticks(api_.user);
        const auto remainingTicks = submittedAt < deadline_ ? deadline_ - submittedAt : 0;
        const auto remainingNs = remainingTicks * 1000000000 / kTickFrequency;
        result = api_.wait(api_.user, event,
                          remainingNs < kWaitLimitNs ? remainingNs : kWaitLimitNs);
        closeResult = api_.close(api_.user, event);
        if (result != 0) {
            // 取消完了と認定せず、静的領域と所有Sessionを終了まで隔離する。
            quarantined_ = true;
            return result;
        }
    }
    // 完了eventはkernelのmapping解除・user buffer復元後に通知される。
    if (words[0] == 0 && words[1] == 0) {
        return words[2] != 0 ? words[2] : kInvalidResponse;
    }
    const auto header0 = words[0];
    const auto header1 = words[1];
    cursor = 2;
    std::uint32_t copyCount = 0;
    std::uint32_t moveCount = 0;
    if (header1 >> 31) {
        const auto special = words[cursor++];
        copyCount = (special >> 1) & 15;
        moveCount = (special >> 5) & 15;
        cursor += (special & 1) ? 2 : 0;
    }
    const auto handles = cursor;
    cursor += copyCount + moveCount;
    cursor += ((header0 >> 16) & 15) * 2;
    cursor += (((header0 >> 20) & 15) + ((header0 >> 24) & 15) + ((header0 >> 28) & 15)) * 3;
    const auto dataWords = header1 & 0x3FF;
    const auto end = cursor + dataWords;
    const auto responseStart = align16(cursor * 4) / 4;
    // 想定外handleは、返却された実handleだけを所有物として解放する。
    const auto discard = [&] {
        for (std::size_t i = handles; i < handles + copyCount + moveCount; ++i) {
            if (words[i] != 0) {
                static_cast<void>(api_.close(api_.user, words[i]));
            }
        }
    };
    if (end > kPageSize / 4 || responseStart + 4 > end || words[responseStart] != 0x4F434653 ||
        words[responseStart + 1] != 0 || words[responseStart + 3] != 0 || (header0 & 0xFFFF) != 0 ||
        (header0 >> 20) != 0) {
        discard();
        return kInvalidResponse;
    }
    result = words[responseStart + 2];
    if (result != 0) {
        discard();
        return result;
    }
    if (closeResult != 0 || copyCount != 0 || moveCount != (object ? 1u : 0u) ||
        responseStart + 4 + (outputSize + 3) / 4 > end || (object && words[handles] == 0)) {
        discard();
        return closeResult != 0 ? closeResult : kInvalidResponse;
    }
    if (object) {
        *object = words[handles];
    }
    copyBytes(output, words + responseStart + 4, outputSize);
    return 0;
}

Result IpcFs::initialize() {
    if (sm_ != 0 || borrowedSm_ != 0 || proxy_ != 0 || quarantined_ || api_.connect == nullptr) {
        return kAlreadyRun;
    }
    // 接続成立前のSession予約失敗だけ、Workerで最大1回再試行する。
    Handle sm = 0;
    Result result = kInvalidResponse;
    constexpr Result kSessionLimit = 0x10801;
    for (std::uint32_t attempt = 1; attempt <= 2; ++attempt) {
        sm = 0;
        recordProgress(Stage::SmConnect, kInProgress);
        result = recordProgress(Stage::SmConnect, api_.connect(api_.user, &sm, "sm:"));
        connectResult_ = result;
#if YW1_BOOTSTRAP_EXTERNAL_DIAGNOSTICS
        diagnostics::yw1_module_diagnostics_state.smConnectAttempts.store(
            attempt, std::memory_order_release);
#endif
        constexpr char kAttempt[] = "[YW1] sm_connect_attempt=";
        constexpr char kResult[] = "[YW1] sm_connect_result=";
        static_cast<void>(memoryLog().appendResult(kAttempt, sizeof(kAttempt) - 1, attempt));
        static_cast<void>(memoryLog().appendResult(kResult, sizeof(kResult) - 1, result));
        if (attempt > 1) {
            recordProgress(Stage::SmRetry, result);
        }
        if (result != kSessionLimit || attempt == 2 || api_.sleep == nullptr) {
            break;
        }
        const auto now = api_.ticks(api_.user);
        if (now >= deadline_ || deadline_ - now <= kTickFrequency / 4) {
            recordProgress(Stage::SmRetry, kDeadline);
            break;
        }
        recordProgress(Stage::SmRetry, kInProgress);
        api_.sleep(api_.user, 250000000);
        if (api_.ticks(api_.user) >= deadline_) {
            recordProgress(Stage::SmRetry, kDeadline);
            break;
        }
    }
    if (result != 0) {
        return result;
    }
    if (sm == 0) {
        return kInvalidResponse;
    }
    sm_ = sm;
    const std::uint64_t pid = 0;
    // 応答が失敗でもRegisterClientの副作用が発生した可能性を保守的に扱う。
    smNeedsDetach_ = true;
    result = call(sm_, 0, &pid, sizeof(pid), nullptr, 0, nullptr, nullptr, nullptr, 0, true);
    if (result == 0) {
        constexpr char kService[8] = "fsp-srv";
        result = call(sm_, 1, kService, sizeof(kService), nullptr, 0, &proxy_);
        serviceResult_ = result;
    }
    if (result == 0) {
        // 通常切断のOnClientDisconnectedは同一PIDのservice登録を後始末し得る。
        // 対象AtmosphereのCMIF DetachClientで、この追加sessionだけを無効化する。
        result = detachSm();
    }
    if (result == 0) {
        result = recordProgress(Stage::SmClose, release(sm_));
    }
    if (result == 0) {
        result = call(proxy_, 1, &pid, sizeof(pid), nullptr, 0, nullptr, nullptr, nullptr, 0, true);
    }
    return result;
}

Result IpcFs::initializeFromSm(Handle borrowedSession) {
    if (borrowedSession == 0 || sm_ != 0 || borrowedSm_ != 0 || proxy_ != 0 || quarantined_) {
        return kInvalidResponse;
    }
    // 既存Sessionは登録変更・Detach・Closeせず、GetService要求だけを発行する。
    borrowedSm_ = borrowedSession;
    constexpr char service[8] = "fsp-srv";
    auto result = serviceResult_ = call(borrowedSm_, 1, service, sizeof(service), nullptr, 0,
                                       &proxy_);
    if (result == 0) {
        const std::uint64_t pid = 0;
        result = call(proxy_, 1, &pid, sizeof(pid), nullptr, 0, nullptr, nullptr, nullptr, 0, true);
    }
    return result;
}

Result IpcFs::openSd() {
    return call(proxy_, 18, nullptr, 0, nullptr, 0, &fs_);
}

Result IpcFs::entryType(const char* path, std::uint32_t& type) {
    return call(fs_, 7, nullptr, 0, &type, sizeof(type), nullptr, path);
}

Result IpcFs::createDirectory(const char* path) {
    return call(fs_, 2, nullptr, 0, nullptr, 0, nullptr, path);
}

Result IpcFs::createFile(const char* path) {
    const std::uint64_t arguments[2]{};
    return call(fs_, 0, arguments, sizeof(arguments), nullptr, 0, nullptr, path);
}

Result IpcFs::openFile(const char* path, bool readable) {
    const std::uint32_t mode = 2 | 4 | (readable ? 1u : 0u);
    return call(fs_, 8, &mode, sizeof(mode), nullptr, 0, &file_, path);
}

Result IpcFs::fileSize(std::uint64_t& size) {
    return call(file_, 4, nullptr, 0, &size, sizeof(size));
}

Result IpcFs::read(std::uint64_t offset, char* data, std::size_t size, std::uint64_t& actual) {
    if (offset > kFileCapacity || size > kFileCapacity - offset || size == 0) {
        return kFileLimit;
    }
    const std::uint64_t arguments[3]{0, offset, size};
    actual = 0;
    const auto result = call(file_, 0, arguments, sizeof(arguments), &actual, sizeof(actual),
                            nullptr, nullptr, nullptr, 0, false, data, size);
    return result == 0 && actual > size ? kInvalidResponse : result;
}

Result IpcFs::write(std::uint64_t offset, const char* data, std::size_t size) {
    if (offset > kFileCapacity || size > kFileCapacity - offset || size == 0) {
        return kFileLimit;
    }
    const std::uint64_t arguments[3]{0, offset, size};
    return call(file_, 1, arguments, sizeof(arguments), nullptr, 0, nullptr, nullptr, data, size);
}

Result IpcFs::flush() {
    return call(file_, 2, nullptr, 0);
}

Result IpcFs::release(Handle& handle) {
    if (handle == 0) {
        return 0;
    }
    const auto result = recordProgress(Stage::SessionClose, api_.close(api_.user, handle));
    handle = 0;
    return result;
}

Result IpcFs::detachSm() {
    if (!smNeedsDetach_) {
        return 0;
    }
    const std::uint64_t pid = 0;
    const auto result =
        call(sm_, 4, &pid, sizeof(pid), nullptr, 0, nullptr, nullptr, nullptr, 0, true);
    if (result == 0) {
        smNeedsDetach_ = false;
    } else {
        // detach完了が不明ならSMを閉じない。ゲームのservice登録を保持する。
        quarantined_ = true;
    }
    return result;
}

Result IpcFs::closeAll() {
    if (quarantined_) {
        return kDeadline;
    }
    const auto detached = detachSm();
    if (detached != 0) {
        return detached;
    }
    Result result = 0;
    Handle* handles[]{&file_, &fs_, &proxy_, &sm_};
    for (auto* handle : handles) {
        const auto closed = release(*handle);
        if (result == 0) {
            result = closed;
        }
    }
    return result;
}

} // 名前空間 yw1::logging
