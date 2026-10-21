#include "sd_writer.hpp"

namespace yw1::logging {
namespace {

template <std::size_t Size> void event(MemoryLog& log, const char (&message)[Size]) {
    static_cast<void>(log.append(message, Size - 1));
}

template <std::size_t Size>
void resultEvent(MemoryLog& log, const char (&label)[Size], Result result) {
    static_cast<void>(log.appendResult(label, Size - 1, result));
}

Result prepareDirectory(IpcFs& fs, const char* path) {
    std::uint32_t type = 0xFFFFFFFF;
    Result result = fs.entryType(path, type);
    if (result == kPathNotFound) {
        result = fs.createDirectory(path);
        if (result != 0 && result != kPathAlreadyExists) {
            return result;
        }
        result = fs.entryType(path, type);
    }
    return result != 0 ? result : (type == 0 ? 0 : kWrongEntry);
}

Result prepareFile(IpcFs& fs) {
    std::uint32_t type = 0xFFFFFFFF;
    Result result = fs.entryType(kLogPath, type);
    if (result == kPathNotFound) {
        result = fs.createFile(kLogPath);
        if (result != 0 && result != kPathAlreadyExists) {
            return result;
        }
        result = fs.entryType(kLogPath, type);
    }
    if (result != 0 || type != 1) {
        return result != 0 ? result : kWrongEntry;
    }
    return fs.openFile(kLogPath);
}

} // 無名名前空間

SaveOutcome SdWriter::save(MemoryLog& log, IpcFs& fs, IpcStorage& storage) {
    SaveOutcome outcome;
    if (entered_.exchange(true, std::memory_order_acq_rel)) {
        outcome.result = kAlreadyRun;
        return outcome;
    }
    event(log, "[YW1 B0.1.2] logger_start\n");
    outcome.result = fs.initialize();
    resultEvent(log, "[YW1 B0.1.2] service_init_result=", outcome.result);
    if (outcome.result == 0) {
        outcome.result = fs.openSd();
        resultEvent(log, "[YW1 B0.1.2] fs_open_result=", outcome.result);
    }
    if (outcome.result == 0) {
        // 指定フォルダまでの不足部分だけを作り、既存ファイルとの衝突を拒否する。
        constexpr const char* kDirectories[]{"/atmosphere", "/atmosphere/contents",
                                             "/atmosphere/contents/0100C0000CEEA000",
                                             "/atmosphere/contents/0100C0000CEEA000/yw1NX-MOD"};
        for (const auto* path : kDirectories) {
            outcome.result = prepareDirectory(fs, path);
            if (outcome.result != 0) {
                break;
            }
        }
    }
    if (outcome.result == 0) {
        outcome.result = prepareFile(fs);
        resultEvent(log, "[YW1 B0.1.2] file_open_result=", outcome.result);
    }
    std::uint64_t offset = 0;
    if (outcome.result == 0) {
        outcome.result = fs.fileSize(offset);
        if (outcome.result == 0 &&
            (offset > kFileCapacity || kLogCapacity + 1024 > kFileCapacity - offset)) {
            // 上限時に削除・truncate・ローテーションはしない。
            outcome.result = kFileLimit;
        }
    }
    LogView view{};
    if (outcome.result == 0) {
        resultEvent(log, "[YW1 B0.1.2] memory_dropped=", log.dropped());
        if (!log.seal(view) || view.size == 0) {
            outcome.result = kBufferFull;
        }
    }
    if (outcome.result == 0) {
        outcome.writeResult = fs.write(offset, view.data, view.size);
        outcome.result = outcome.writeResult;
        if (outcome.result == 0) {
            outcome.flushResult = fs.flush();
            outcome.result = outcome.flushResult;
            outcome.payloadPersisted = outcome.flushResult == 0;
        }
        if (!fs.quarantined()) {
            // この追記は直前のpayloadの実Resultだけを表す。自身の永続化は先取りしない。
            auto* receipt = storage.receipt;
            std::size_t size = 0;
            const auto add = [&](const char* label, std::size_t length, Result result) {
                size += formatResult(receipt + size, kPageSize - size, label, length, result);
            };
            constexpr char kWrite[] = "[YW1 B0.1.2] file_write_result=";
            constexpr char kFlush[] = "[YW1 B0.1.2] file_flush_result=";
            add(kWrite, sizeof(kWrite) - 1, outcome.writeResult);
            if (outcome.writeResult == 0) {
                add(kFlush, sizeof(kFlush) - 1, outcome.flushResult);
                if (outcome.payloadPersisted) {
                    constexpr char kComplete[] = "[YW1 B0.1.2] payload_flush_completed\n";
                    for (std::size_t i = 0; i < sizeof(kComplete) - 1; ++i) {
                        receipt[size++] = kComplete[i];
                    }
                }
                // Write失敗時は部分的書込みの可能性があるため位置を推測して追記しない。
                outcome.receiptWriteResult = fs.write(offset + view.size, receipt, size);
                if (outcome.receiptWriteResult == 0) {
                    outcome.receiptFlushResult = fs.flush();
                    outcome.receiptPersisted = outcome.receiptFlushResult == 0;
                }
                if (outcome.result == 0) {
                    outcome.result = outcome.receiptWriteResult != 0 ? outcome.receiptWriteResult
                                                                     : outcome.receiptFlushResult;
                }
            }
        }
    }
    // CloseHandleはkernel側の解放結果。FSサーバーの遠隔destructor完了とは区別する。
    outcome.closeResult = fs.closeAll();
    outcome.quarantined = fs.quarantined();
    if (outcome.result == 0) {
        outcome.result = outcome.closeResult;
    }
    return outcome;
}

} // 名前空間 yw1::logging
