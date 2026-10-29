#include "snapshot_writer.hpp"

namespace yw1::logging {
namespace {
Result directory(IpcFs& fs, const char* path) {
    std::uint32_t type = UINT32_MAX;
    auto result = fs.entryType(path, type);
    if (result == kPathNotFound) {
        result = fs.createDirectory(path);
        if (result != 0 && result != kPathAlreadyExists) { return result; }
        result = fs.entryType(path, type);
    }
    return result != 0 ? result : type == 0 ? 0 : kWrongEntry;
}
} // 無名名前空間

Result SnapshotWriter::recover(IpcFs& fs, SnapshotIo& io) {
    std::uint64_t size = 0;
    auto result = fs.fileSize(size);
    if (result != 0 || size == 0) { return result; }
    // 他形式と既存の大きなログは、手動退避まで一切変更しない。
    if (size > kSnapshotFileSize) { return kForeignLog; }
    bool recognized = false;
    bool committed = false;
    for (unsigned i = 0; i < 2; ++i) {
        const auto offset = i * kSnapshotSlotSize;
        if (offset >= size) { continue; }
        for (char& byte : io.readback) { byte = ' '; }
        const auto count = size - offset < kSnapshotSlotSize ? size - offset : kSnapshotSlotSize;
        std::uint64_t actual = 0;
        result = fs.read(offset, io.readback.data(), count, actual);
        outcome_.read = result;
        if (result != 0) { return result; }
        if (actual != count) { return kShortRead; }
        const auto record = inspectSnapshot(io.readback);
        recognized |= record.recognized;
        if (record.committed && (!committed || record.sequence > sequence_)) {
            sequence_ = record.sequence;
            slot_ = i ^ 1;
            committed = true;
        }
    }
    if (!recognized) { return kForeignLog; }
    if (!committed) { slot_ = size <= kSnapshotSlotSize ? 1 : 0; }
    outcome_.verifiedSequence = sequence_;
    return 0;
}

Result SnapshotWriter::open(IpcFs& fs, SnapshotIo& io, Handle borrowedSm) {
    if (entered_) { return kAlreadyRun; }
    entered_ = true;
    outcome_.result = outcome_.initialize = borrowedSm != 0 ? fs.initializeFromSm(borrowedSm)
                                                           : fs.initialize();
    if (outcome_.result == 0) { outcome_.result = outcome_.sdOpen = fs.openSd(); }
    if (outcome_.result == 0) {
        constexpr const char* directories[]{"/atmosphere", "/atmosphere/contents",
            "/atmosphere/contents/0100C0000CEEA000",
            "/atmosphere/contents/0100C0000CEEA000/yw1NX-MOD"};
        for (const auto* path : directories) {
            outcome_.result = directory(fs, path);
            if (outcome_.result != 0) { break; }
        }
    }
    if (outcome_.result == 0) {
        std::uint32_t type = UINT32_MAX;
        outcome_.result = fs.entryType(kLogPath, type);
        if (outcome_.result == kPathNotFound) {
            outcome_.result = fs.createFile(kLogPath);
            if (outcome_.result == 0 || outcome_.result == kPathAlreadyExists) {
                outcome_.result = fs.entryType(kLogPath, type);
            }
        }
        if (outcome_.result == 0 && type != 1) { outcome_.result = kWrongEntry; }
    }
    if (outcome_.result == 0) {
        outcome_.result = outcome_.fileOpen = fs.openFile(kLogPath, true);
    }
    if (outcome_.result == 0) { outcome_.result = recover(fs, io); }
    ready_ = outcome_.result == 0;
    return outcome_.result;
}

Result SnapshotWriter::save(IpcFs& fs, SnapshotIo& io, const NativeSnapshot& snapshot) {
    if (!ready_) { return kAlreadyRun; }
    ready_ = false;
    outcome_.write = outcome_.flush = outcome_.receiptWrite = outcome_.receiptFlush = kNotRun;
    outcome_.read = kNotRun;
    if (sequence_ == UINT64_MAX) { return outcome_.result = kSequenceLimit; }
    const auto next = sequence_ + 1;
    if (!formatSnapshot(io.frame, snapshot, next)) { return outcome_.result = kBufferFull; }
    const auto offset = slot_ * kSnapshotSlotSize;
    outcome_.result = outcome_.write = fs.write(offset, io.frame.data(), io.frame.size());
    if (outcome_.result != 0) { return outcome_.result; }
    outcome_.result = outcome_.flush = fs.flush();
    if (outcome_.result != 0) { return outcome_.result; }
    // 直前の有効slotを残し、payload永続化後にだけcommit receiptを公開する。
    commitSnapshot(io.frame);
    outcome_.result = outcome_.receiptWrite = fs.write(offset + kSnapshotPayloadSize,
        io.frame.data() + kSnapshotPayloadSize, kSnapshotFooterSize);
    if (outcome_.result != 0) { return outcome_.result; }
    outcome_.result = outcome_.receiptFlush = fs.flush();
    if (outcome_.result != 0) { return outcome_.result; }
    std::uint64_t actual = 0;
    outcome_.result = outcome_.read = fs.read(offset, io.readback.data(), io.readback.size(), actual);
    if (outcome_.result != 0) { return outcome_.result; }
    if (actual != io.readback.size()) { return outcome_.result = kShortRead; }
    const auto record = inspectSnapshot(io.readback);
    if (!record.committed || record.sequence != next) {
        return outcome_.result = kSnapshotInvalid;
    }
    for (std::size_t i = 0; i < io.frame.size(); ++i) {
        if (io.frame[i] != io.readback[i]) { return outcome_.result = kSnapshotInvalid; }
    }
    sequence_ = next;
    slot_ ^= 1;
    outcome_.verifiedSequence = sequence_;
    ready_ = true;
    return outcome_.result = 0;
}
} // 名前空間 yw1::logging
