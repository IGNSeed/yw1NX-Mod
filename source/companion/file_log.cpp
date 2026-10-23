#include "report.hpp"

#include <switch.h>

namespace yw1::companion {
namespace {
constexpr Result kPathNotFound = 0x202;
constexpr Result kPathAlreadyExists = 0x402;

Result prepareDirectory(FsFileSystem& fs, const char* path) {
    FsDirEntryType type{};
    auto rc = fsFsGetEntryType(&fs, path, &type);
    if (rc == kPathNotFound) {
        rc = fsFsCreateDirectory(&fs, path);
        if (rc != 0 && rc != kPathAlreadyExists) {
            return rc;
        }
        rc = fsFsGetEntryType(&fs, path, &type);
    }
    return rc != 0 ? rc : (type == FsDirEntryType_Dir ? 0 : kInvalidData);
}
} // 無名名前空間

FileOutcome saveReport(Report& report) {
    FileOutcome outcome;
    FsFileSystem fs{};
    outcome.sdOpen = report.result("companion_sd_open_result", fsOpenSdCardFileSystem(&fs));
    if (outcome.sdOpen != 0) {
        return outcome;
    }
    constexpr const char* kDirectories[]{"/atmosphere", "/atmosphere/contents",
                                         "/atmosphere/contents/0100C0000CEEA000",
                                         "/atmosphere/contents/0100C0000CEEA000/yw1NX-MOD"};
    for (auto* path : kDirectories) {
        outcome.directory = prepareDirectory(fs, path);
        report.append("directory=%s result=0x%08X\n", path, outcome.directory);
        if (outcome.directory != 0) {
            fsFsClose(&fs);
            return outcome;
        }
    }
    FsDirEntryType type{};
    auto rc = fsFsGetEntryType(&fs, kLogPath, &type);
    if (rc == kPathNotFound) {
        rc = report.result("companion_create_file_result", fsFsCreateFile(&fs, kLogPath, 0, 0));
        if (rc == 0 || rc == kPathAlreadyExists) {
            rc = fsFsGetEntryType(&fs, kLogPath, &type);
        }
    }
    if (rc != 0 || type != FsDirEntryType_File) {
        outcome.fileOpen = report.result("companion_file_open_result", rc != 0 ? rc : kInvalidData);
        fsFsClose(&fs);
        return outcome;
    }
    FsFile file{};
    outcome.fileOpen =
        report.result("companion_file_open_result",
                      fsFsOpenFile(&fs, kLogPath, FsOpenMode_Write | FsOpenMode_Append, &file));
    if (outcome.fileOpen != 0) {
        fsFsClose(&fs);
        return outcome;
    }
    s64 offset = 0;
    rc = report.result("companion_file_size_result", fsFileGetSize(&file, &offset));
    if (rc == 0 && (offset < 0 || static_cast<std::uint64_t>(offset) > kFileCapacity ||
                    report.size + 512 > kFileCapacity - static_cast<std::uint64_t>(offset))) {
        rc = report.result("companion_file_limit_result", kFileLimit);
    }
    outcome.prepare = report.truncated ? kInvalidData : rc;
    if (rc == 0 && !report.truncated) {
        outcome.write = fsFileWrite(&file, offset, report.text, report.size, 0);
        if (outcome.write == 0) {
            outcome.flush = fsFileFlush(&file);
            char receipt[256];
            const auto length = std::snprintf(
                receipt, sizeof(receipt),
                "source=companion-nro payload_write_result=0x%08X payload_flush_result=0x%08X\n",
                outcome.write, outcome.flush);
            if (length > 0 && static_cast<std::size_t>(length) < sizeof(receipt)) {
                outcome.receiptWrite = fsFileWrite(&file, offset + report.size, receipt, length, 0);
                if (outcome.receiptWrite == 0) {
                    outcome.receiptFlush = fsFileFlush(&file);
                }
            }
        }
    }
    fsFileClose(&file);
    fsFsClose(&fs);
    outcome.closeIssued = true;
    report.result("companion_payload_write_result", outcome.write);
    report.result("companion_payload_flush_result", outcome.flush);
    report.result("companion_receipt_write_result", outcome.receiptWrite);
    report.result("companion_receipt_flush_result", outcome.receiptFlush);
    // libnxのCloseはvoid。遠隔CloseのResultを0と捏造しない。
    return outcome;
}

} // 名前空間 yw1::companion
