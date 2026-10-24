#include "report.hpp"

namespace yw1::companion {
std::uint32_t snapshotPreflight(const GotObservation& got, CodeSnapshot& output) {
    output.preflightAttempted = true;
    output.preflight = kInvalidData;
    if (!got.mainVerified || got.result != 0 || got.readResult != 0 || !got.valueKnown ||
        got.slotValue == 0 || got.slotValue % 4 != 0) {
        return output.preflight;
    }
    const auto& memory = got.memory;
    if (!memory.queryAttempted || memory.queryResult != 0 || !memory.infoReturned ||
        !memory.regionValid || memory.validationResult != 0 || memory.containsPointer != 1 ||
        memory.addressBaseResult != 0 || memory.addressSizeResult != 0 ||
        !validTargetAddress(got.slotValue, memory.addressSpaceBase, memory.addressSpaceSize)) {
        return output.preflight;
    }
    const auto& region = memory.region;
    // typeだけではKernel内部flagを証明しない。Map SVC自身にも実Stateを検査させる。
    if (!memory.typeKnown || !memory.attributesKnown || !memory.permissionKnown ||
        memory.mapped != 1 || memory.readable != 1 || memory.executable != 1 ||
        memory.writable != 0 || region.type != 8 || region.attributes != 0 ||
        region.permission != 5 || region.ipcRefcount != 0 || region.deviceRefcount != 0) {
        output.preflight = kSnapshotUnsupported;
        return output.preflight;
    }
    const auto page = got.slotValue & ~std::uint64_t{4095};
    if (!diagnostics::contains(region.base, region.size, got.slotValue, kSnapshotLength) ||
        !diagnostics::contains(region.base, region.size, page, 4096) ||
        got.slotValue % 4096 > 4096 - kSnapshotLength ||
        got.slotValue > UINT64_MAX - (kSnapshotLength - 1) ||
        !validTargetAddress(got.slotValue + kSnapshotLength - 1, memory.addressSpaceBase,
                            memory.addressSpaceSize)) {
        return output.preflight;
    }
    output.pageAddress = page;
    output.pageOffset = static_cast<std::uint32_t>(got.slotValue - page);
    output.preflight = 0;
    return 0;
}

bool sameSnapshotRegion(const TargetRegion& left, const TargetRegion& right) {
    return left.base == right.base && left.size == right.size && left.type == right.type &&
           left.attributes == right.attributes && left.permission == right.permission &&
           left.ipcRefcount == right.ipcRefcount && left.deviceRefcount == right.deviceRefcount;
}

const char* snapshotConsistency(const CodeSnapshot& snapshot) {
    if (snapshot.bytesValid) {
        return "two_equal_reads_and_pre_post_checks_not_atomic";
    }
    return snapshot.readsMatched ? "two_equal_reads_not_accepted" : "not_validated";
}

void appendSnapshotReport(Report& report, const Observation& observation) {
    const auto& got = observation.got;
    const auto& snapshot = got.snapshot;
    const auto& memory = got.memory;
    report.append("\n=== YW1 EGL GOT Code Snapshot B0.2.4 ===\nsource=companion-nro\n"
                  "mode=read-only observation_stage=album_snapshot\n"
                  "snapshot_preflight_attempted=%u\n",
                  snapshot.preflightAttempted);
    report.result("snapshot_result", snapshot.result);
    report.result("snapshot_preflight_result", snapshot.preflight);
    if (got.valueKnown) {
        const auto relative = relativeAddress(got.slotValue, got.mainBase);
        report.append("snapshot_target_address=0x%016llX\n"
                      "snapshot_target_relative_to_main=%s0x%llX\n",
                      static_cast<unsigned long long>(got.slotValue), relative.negative ? "-" : "+",
                      static_cast<unsigned long long>(relative.magnitude));
    } else {
        report.append("snapshot_target_address=unknown snapshot_target_relative_to_main=unknown\n");
    }
    if (memory.regionValid && got.valueKnown) {
        report.append("snapshot_target_relative_to_region=+0x%llX\n"
                      "snapshot_region_type=0x%08X snapshot_region_permission=0x%08X\n"
                      "snapshot_region_attributes=0x%08X\n",
                      static_cast<unsigned long long>(got.slotValue - memory.region.base),
                      memory.region.type, memory.region.permission, memory.region.attributes);
    } else {
        report.append("snapshot_target_relative_to_region=unknown snapshot_region_type=unknown\n"
                      "snapshot_region_permission=unknown snapshot_region_attributes=unknown\n");
    }
    if (snapshot.preflight == 0) {
        report.append("snapshot_page_address=0x%016llX snapshot_page_offset=0x%X\n",
                      static_cast<unsigned long long>(snapshot.pageAddress), snapshot.pageOffset);
    } else {
        report.append("snapshot_page_address=unknown snapshot_page_offset=unknown\n");
    }
    report.append("snapshot_length=16\n");
    report.result("snapshot_revalidation_result", snapshot.revalidation);
    report.result("snapshot_before_pid_result", snapshot.beforePid);
    report.result("snapshot_before_title_result", snapshot.beforeTitle);
    report.result("snapshot_before_got_result", snapshot.beforeGot);
    report.result("snapshot_before_query_result", snapshot.beforeQuery);
    report.append("snapshot_map_attempted=%u\n", snapshot.mapAttempted);
    report.result("snapshot_map_result", snapshot.map);
    report.append("snapshot_read_attempted=%u\n", snapshot.readAttempted);
    report.result("snapshot_read_result", snapshot.read);
    report.append("snapshot_unmap_attempted=%u\n", snapshot.unmapAttempted);
    report.result("snapshot_unmap_result", snapshot.unmap);
    report.result("snapshot_postvalidation_result", snapshot.postvalidation);
    report.result("snapshot_after_pid_result", snapshot.afterPid);
    report.result("snapshot_after_title_result", snapshot.afterTitle);
    report.result("snapshot_after_got_result", snapshot.afterGot);
    report.result("snapshot_after_query_result", snapshot.afterQuery);
    report.append("snapshot_cleanup_quarantined=%u snapshot_bytes_valid=%s\nsnapshot_bytes_hex=",
                  snapshot.cleanupQuarantined, targetBoolean(snapshot.bytesValid));
    if (snapshot.bytesValid) {
        for (auto byte : snapshot.bytes) {
            report.append("%02X", byte);
        }
    } else {
        report.append("unknown");
    }
    report.append("\nsnapshot_consistency=%s\n"
                  "got_hook_install_attempted=0 got_modification=disabled\n",
                  snapshotConsistency(snapshot));
}
} // 名前空間 yw1::companion
