#include "diagnostic_target.hpp"
#include "report.hpp"

#include <cstring>
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
bool openLogFile(Report& report, FileOutcome& outcome, FsFileSystem& fs, FsFile& file, int mode) {
    outcome.sdOpen = report.result("companion_sd_open_result", fsOpenSdCardFileSystem(&fs));
    if (outcome.sdOpen != 0) {
        return false;
    }
    constexpr const char* kDirectories[]{"/atmosphere", "/atmosphere/contents",
                                         "/atmosphere/contents/0100C0000CEEA000",
                                         "/atmosphere/contents/0100C0000CEEA000/yw1NX-MOD"};
    for (auto* path : kDirectories) {
        outcome.directory = prepareDirectory(fs, path);
        report.append("directory=%s result=0x%08X\n", path, outcome.directory);
        if (outcome.directory != 0) {
            fsFsClose(&fs);
            return false;
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
        return false;
    }
    outcome.fileOpen =
        report.result("companion_file_open_result", fsFsOpenFile(&fs, kLogPath, mode, &file));
    if (outcome.fileOpen != 0) {
        fsFsClose(&fs);
        return false;
    }
    return true;
}
} // 無名名前空間

FileOutcome saveReport(Report& report, std::size_t startOffset) {
    FileOutcome outcome;
    if (startOffset > report.size) {
        outcome.prepare = kInvalidData;
        return outcome;
    }
    FsFileSystem fs{};
    FsFile file{};
    if (!openLogFile(report, outcome, fs, file, FsOpenMode_Write | FsOpenMode_Append)) {
        return outcome;
    }
    s64 offset = 0;
    auto rc = report.result("companion_file_size_result", fsFileGetSize(&file, &offset));
    if (rc == 0 &&
        (offset < 0 || static_cast<std::uint64_t>(offset) > kFileCapacity ||
         report.size - startOffset + 512 > kFileCapacity - static_cast<std::uint64_t>(offset))) {
        rc = report.result("companion_file_limit_result", kFileLimit);
    }
    outcome.prepare = report.truncated ? kInvalidData : rc;
    if (rc == 0 && !report.truncated) {
        const auto end = report.size;
        const auto length = end - startOffset;
        outcome.write = fsFileWrite(&file, offset, report.text + startOffset, length, 0);
        if (outcome.write == 0) {
            outcome.flush = fsFileFlush(&file);
            if (outcome.flush == 0) {
                outcome.savedThrough = end;
            }
            char receipt[256];
            const auto length = std::snprintf(
                receipt, sizeof(receipt),
                "source=companion-nro payload_write_result=0x%08X payload_flush_result=0x%08X\n",
                outcome.write, outcome.flush);
            if (length > 0 && static_cast<std::size_t>(length) < sizeof(receipt)) {
                outcome.receiptWrite =
                    fsFileWrite(&file, offset + end - startOffset, receipt, length, 0);
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

std::uint32_t saveBootstrapLog(Report& report, BootstrapLog& bootstrap) {
    if (bootstrap.readResult != 0 || !diagnostics::validExportHeader(bootstrap.header) ||
        !diagnostics::validExportText(bootstrap.text, bootstrap.header.used) ||
        diagnostics::logChecksum(bootstrap.text, bootstrap.header.used) !=
            bootstrap.header.checksum) {
        return report.result("bootstrap_export_save_result", kExportInvalid);
    }
    char build[65];
    for (std::size_t i = 0; i < 32; ++i) {
        std::snprintf(build + i * 2, 3, "%02X", kExpectedModuleId[i]);
    }
    char marker[256];
    const auto markerSize = std::snprintf(
        marker, sizeof(marker),
        "bootstrap_export_committed pid=%llu build=%s base=%llX boot=%llX used=%u checksum=%08X\n",
        static_cast<unsigned long long>(bootstrap.pid), build,
        static_cast<unsigned long long>(bootstrap.moduleBase),
        static_cast<unsigned long long>(bootstrap.header.bootToken), bootstrap.header.used,
        bootstrap.header.checksum);
    if (markerSize <= 0 || static_cast<std::size_t>(markerSize) >= sizeof(marker)) {
        return kInvalidData;
    }
    FsFileSystem fs{};
    FsFile file{};
    FileOutcome outcome;
    if (!openLogFile(report, outcome, fs, file,
                     FsOpenMode_Read | FsOpenMode_Write | FsOpenMode_Append)) {
        return report.result("bootstrap_export_open_result",
                             outcome.sdOpen != 0
                                 ? outcome.sdOpen
                                 : (outcome.directory != 0 ? outcome.directory : outcome.fileOpen));
    }
    s64 fileSize = 0;
    auto rc = report.result("bootstrap_export_file_size_result", fsFileGetSize(&file, &fileSize));
    if (rc == 0 && (fileSize < 0 || static_cast<std::uint64_t>(fileSize) > kFileCapacity)) {
        rc = kFileLimit;
    }
    // 自分の固定pathの1MiB以内を有限chunkで確認し、以前flush済みの同一prefixを再保存しない。
    char scan[4096 + 256];
    std::size_t overlap = 0;
    bool duplicate = false;
    for (s64 offset = 0; rc == 0 && offset < fileSize;) {
        const auto length = static_cast<std::uint64_t>(fileSize - offset) < 4096
                                ? static_cast<std::uint64_t>(fileSize - offset)
                                : 4096;
        std::uint64_t read = 0;
        rc = fsFileRead(&file, offset, scan + overlap, length, 0, &read);
        if (rc != 0 || read != length) {
            if (rc == 0) {
                rc = kInvalidData;
            }
            break;
        }
        const auto total = overlap + static_cast<std::size_t>(read);
        for (std::size_t i = 0; i + markerSize <= total; ++i) {
            if (std::memcmp(scan + i, marker, markerSize) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            break;
        }
        overlap = total < static_cast<std::size_t>(markerSize - 1) ? total : markerSize - 1;
        std::memmove(scan, scan + total - overlap, overlap);
        offset += static_cast<s64>(length);
    }
    report.result("bootstrap_duplicate_scan_result", rc);
    bootstrap.duplicate = duplicate;
    if (rc == 0 && !duplicate) {
        char heading[384];
        const auto headingSize = std::snprintf(
            heading, sizeof(heading),
            "\n=== YW1 Bootstrap Export B0.1.6 "
            "===\nsource=module-bootstrap\ntransport=companion-nro\n"
            "boot=%llX pid=%llu generation=%llu dropped=%u flags=%u\noriginal_memory_log_begin\n",
            static_cast<unsigned long long>(bootstrap.header.bootToken),
            static_cast<unsigned long long>(bootstrap.pid),
            static_cast<unsigned long long>(bootstrap.header.generation), bootstrap.header.dropped,
            bootstrap.header.flags);
        constexpr char kEnd[] = "\noriginal_memory_log_end\n";
        const auto required = static_cast<std::uint64_t>(headingSize) + bootstrap.header.used +
                              sizeof(kEnd) - 1 + markerSize;
        if (headingSize <= 0 || static_cast<std::size_t>(headingSize) >= sizeof(heading) ||
            required > kFileCapacity - static_cast<std::uint64_t>(fileSize)) {
            rc = kFileLimit;
        }
        if (rc == 0) {
            rc = report.result("bootstrap_heading_write_result",
                               fsFileWrite(&file, fileSize, heading, headingSize, 0));
        }
        if (rc == 0) {
            rc = report.result("bootstrap_text_write_result",
                               fsFileWrite(&file, fileSize + headingSize, bootstrap.text,
                                           bootstrap.header.used, 0));
        }
        if (rc == 0) {
            rc = report.result("bootstrap_footer_write_result",
                               fsFileWrite(&file, fileSize + headingSize + bootstrap.header.used,
                                           kEnd, sizeof(kEnd) - 1, 0));
        }
        if (rc == 0) {
            rc = report.result("bootstrap_payload_flush_result", fsFileFlush(&file));
        }
        // markerはpayload flush成功後だけ書く。自身のflush結果はNRO checkpointへ残す。
        if (rc == 0) {
            rc = report.result(
                "bootstrap_commit_write_result",
                fsFileWrite(&file, fileSize + required - markerSize, marker, markerSize, 0));
        }
        if (rc == 0) {
            rc = report.result("bootstrap_commit_flush_result", fsFileFlush(&file));
        }
    }
    fsFileClose(&file);
    fsFsClose(&fs);
    report.append("bootstrap_file_close_issued=true duplicate_export_skipped=%s\n",
                  duplicate ? "true" : "false");
    return report.result("bootstrap_export_save_result", rc);
}

} // 名前空間 yw1::companion
