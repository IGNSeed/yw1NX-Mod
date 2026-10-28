#include "report.hpp"

#include "diagnostic_target.hpp"

namespace yw1::companion {
std::uint32_t deriveOriginalStorage(const GotObservation& got,
                                  std::span<const std::uint8_t, 32> digest,
                                  OriginalEglObservation& output) {
    output.identity = kOriginalIdentityMismatch;
    if (!got.mainVerified || got.result != 0 || !got.valueKnown ||
        !got.snapshot.bytesValid || got.snapshot.result != 0 ||
        got.slotValue < kSaltySwapOffset || !got.memory.regionValid ||
        got.memory.addressBaseResult != 0 || got.memory.addressSizeResult != 0) {
        return output.identity;
    }
    for (std::size_t i = 0; i < kSaltySwapDigest.size(); ++i) {
        if (digest[i] != kSaltySwapDigest[i]) {
            return output.identity;
        }
    }
    const auto base = got.slotValue - kSaltySwapOffset;
    if (base == 0 || base % 4096 != 0 || base != got.memory.region.base ||
        got.memory.region.size != 0x19000 || got.memory.region.type != 8 ||
        got.memory.region.permission != 5 || got.memory.region.attributes != 0 ||
        got.memory.region.ipcRefcount != 0 || got.memory.region.deviceRefcount != 0 ||
        base > UINT64_MAX - kSaltyRwOffset - kSaltyRwSize ||
        !validTargetAddress(base + kSaltyRwOffset + kSaltyRwSize - 1,
                            got.memory.addressSpaceBase, got.memory.addressSpaceSize)) {
        return output.identity;
    }
    output.coreBase = base;
    output.storageAddress = base + kOriginalStorageOffset;
    output.coreKnown = true;
    output.identity = 0;
    return 0;
}

std::uint32_t originalStoragePreflight(const GotObservation& got,
                                      OriginalEglObservation& output) {
    output.preflight = kOriginalStorageUnsupported;
    const auto& memory = output.storage;
    const auto& region = memory.region;
    if (!output.coreKnown || output.identity != 0 ||
        output.coreBase > UINT64_MAX - kSaltyRwOffset - kSaltyRwSize ||
        output.storageAddress != output.coreBase + kOriginalStorageOffset ||
        output.storageAddress > UINT64_MAX - 7 ||
        output.storageAddress % 8 != 0 || memory.queryResult != 0 ||
        !memory.infoReturned || !memory.regionValid || memory.validationResult != 0 ||
        !validTargetAddress(output.storageAddress, got.memory.addressSpaceBase,
                            got.memory.addressSpaceSize) ||
        !validTargetAddress(output.storageAddress + 7, got.memory.addressSpaceBase,
                            got.memory.addressSpaceSize)) {
        return output.preflight;
    }
    const auto page = output.storageAddress & ~std::uint64_t{4095};
    // 固定ELFのRW範囲内にあるAliasCodeDataだけを認め、Kernelにも実flagを検査させる。
    if (region.type != 9 || region.permission != 3 || region.attributes != 0 ||
        region.ipcRefcount != 0 || region.deviceRefcount != 0 ||
        !memory.typeKnown || !memory.attributesKnown || !memory.permissionKnown ||
        memory.readable != 1 || memory.writable != 1 || memory.executable != 0 ||
        !diagnostics::contains(output.coreBase + kSaltyRwOffset, kSaltyRwSize,
                               region.base, region.size) ||
        !diagnostics::contains(region.base, region.size, page, 4096) ||
        !diagnostics::contains(region.base, region.size, output.storageAddress, 8) ||
        output.storageAddress % 4096 > 4088) {
        return output.preflight;
    }
    output.pageAddress = page;
    output.pageOffset = static_cast<std::uint32_t>(output.storageAddress - page);
    output.preflight = 0;
    return 0;
}

void classifyOriginalPointer(std::span<const ModuleRecord> modules, const GotObservation& got,
                             OriginalEglObservation& output) {
    if (!output.snapshotValid) {
        return;
    }
    if (output.pointerInfoReturned && output.target.regionValid &&
        output.target.permissionKnown) {
        output.executable = output.target.executable;
    }
    const auto pointer = output.pointer;
    if (pointer == 0) { output.kind = OriginalPointerKind::Null; return; }
    if (pointer % 4 != 0) { output.kind = OriginalPointerKind::Unaligned; return; }
    if (!validTargetAddress(pointer, got.memory.addressSpaceBase, got.memory.addressSpaceSize)) {
        output.kind = OriginalPointerKind::OutsideAddressSpace;
        return;
    }
    if (pointer == got.slotValue) { output.kind = OriginalPointerKind::SwapSelf; return; }
    if (diagnostics::contains(output.coreBase, kSaltyRwOffset + kSaltyRwSize, pointer, 4)) {
        output.kind = OriginalPointerKind::CoreInternal;
        return;
    }
    if (!validTargetModules(modules)) {
        output.kind = OriginalPointerKind::AmbiguousModule;
        return;
    }
    for (std::size_t i = 0; i < modules.size(); ++i) {
        if (diagnostics::contains(modules[i].base, modules[i].size, pointer, 4)) {
            output.moduleKnown = true;
            output.moduleIndex = static_cast<int>(i);
            output.moduleOffset = pointer - modules[i].base;
            for (std::size_t j = 0; j < 32; ++j) { output.moduleId[j] = modules[i].buildId[j]; }
            GotObservation classification;
            classification.slotValue = pointer;
            classification.valueKnown = true;
            classifyGot(modules, classification);
            output.moduleName = gotTargetName(classification.target);
            output.expectedExport = equalId(modules[i].buildId, diagnostics::kSubsdk0Id.data()) &&
                                    output.moduleOffset == 0x10B60;
            break;
        }
    }
    if (output.pointerQuery != 0 || !output.target.regionValid ||
        output.target.validationResult != 0 || !output.target.permissionKnown ||
        !output.target.typeKnown || !output.target.attributesKnown || output.target.mapped != 1) {
        output.kind = OriginalPointerKind::InvalidRegion;
        return;
    }
    output.executable = output.target.executable;
    if (output.executable != 1 || output.target.readable != 1 ||
        !diagnostics::contains(output.target.region.base, output.target.region.size, pointer, 4)) {
        output.kind = OriginalPointerKind::NonExecutable;
    } else if (output.expectedExport == 1) {
        output.kind = OriginalPointerKind::ExpectedSubsdk0Export;
    } else {
        output.kind = output.moduleKnown ? OriginalPointerKind::LoadedModule
                                        : OriginalPointerKind::OtherExecutable;
    }
}

const char* originalPointerKindName(OriginalPointerKind kind) {
    constexpr const char* names[]{"unknown", "null", "unaligned", "outside_address_space",
        "swap_self_recursion", "core_internal_recursion", "invalid_or_unqueried_region",
        "non_executable", "expected_subsdk0_export", "loaded_module_other_address",
        "other_executable_region", "ambiguous_module_list"};
    const auto index = static_cast<unsigned>(kind);
    return index < std::size(names) ? names[index] : "unknown";
}

void appendOriginalReport(Report& report, const Observation& observation) {
    const auto& got = observation.got;
    const auto& original = got.original;
    report.append("\n=== YW1 SaltyNX Original EGL B0.2.7 ===\nsource=companion-nro\n"
                  "mode=read-only\napplication_pid=%llu main_build_id_verified=%s\n"
                  "saltynx_reference_version=1.9.3 core_identity_scope=64_byte_digest_and_layout\n"
                  "saltynx_loaded_image_full_hash=not_verified\n",
                  static_cast<unsigned long long>(observation.pid), targetBoolean(got.mainVerified));
    report.result("original_result", original.result);
    report.result("original_core_identity_result", original.identity);
    if (original.coreKnown) {
        report.append("got_slot_value=0x%016llX saltynx_core_base=0x%016llX\n"
                      "saltynx_swap_offset=0x1E80 original_storage_address=0x%016llX\n"
                      "original_storage_relative_to_core=0x2A3B8\n",
                      static_cast<unsigned long long>(got.slotValue),
                      static_cast<unsigned long long>(original.coreBase),
                      static_cast<unsigned long long>(original.storageAddress));
    } else {
        report.append("saltynx_core_base=unknown original_storage_address=unknown\n");
    }
    const auto appendMemory = [&](const char* name, bool known, const TargetMemoryObservation& memory) {
        if (known) {
            const auto& region = memory.region;
            report.append("%s_region_base=0x%016llX %s_region_size=0x%llX\n"
                          "%s_region_type=0x%X %s_permission=0x%X %s_attributes=0x%X\n"
                          "%s_ipc_refcount=%u %s_device_refcount=%u\n",
                          name, static_cast<unsigned long long>(region.base), name,
                          static_cast<unsigned long long>(region.size), name, region.type,
                          name, region.permission, name, region.attributes, name, region.ipcRefcount,
                          name, region.deviceRefcount);
        } else {
            report.append("%s_region_base=unknown %s_region_type=unknown %s_permission=unknown\n",
                          name, name, name);
        }
    };
    appendMemory("original_storage", original.storageInfoReturned, original.storage);
    if (original.storageInfoReturned) {
        report.append("original_storage_region_permission=0x%X\n",
                      original.storage.region.permission);
    } else {
        report.append("original_storage_region_permission=unknown\n");
    }
    report.result("original_storage_query_result", original.storage.queryResult);
    report.result("original_storage_validation_result", original.storage.validationResult);
    report.append("original_storage_mapped=%s original_storage_readable=%s "
                  "original_storage_writable=%s\n",
                  targetBoolean(original.storage.mapped), targetBoolean(original.storage.readable),
                  targetBoolean(original.storage.writable));
    report.result("original_preflight_result", original.preflight);
    report.result("original_before_validation_result", original.beforeValidation);
    report.result("original_before_storage_result", original.beforeStorage);
    report.append("original_map_attempted=%u original_read_attempted=%u original_unmap_attempted=%u\n",
                  original.mapAttempted, original.readAttempted, original.unmapAttempted);
    report.result("original_map_result", original.map);
    report.result("original_read_result", original.read);
    report.result("original_unmap_result", original.unmap);
    report.result("original_after_storage_result", original.afterStorage);
    report.result("original_postvalidation_result", original.postvalidation);
    report.result("original_pointer_query_result", original.pointerQuery);
    report.result("original_pointer_validation_result", original.target.validationResult);
    report.append("original_pointer_value=");
    if (original.snapshotValid) {
        report.append("0x%016llX\n", static_cast<unsigned long long>(original.pointer));
    } else {
        report.append("unknown\n");
    }
    appendMemory("original_pointer", original.pointerInfoReturned, original.target);
    report.append("original_pointer_module=");
    if (original.moduleKnown) {
        report.append("%s original_pointer_loader_index=%d original_pointer_relative_offset=0x%llX\n"
                      "original_pointer_module_full_build_id=", original.moduleName, original.moduleIndex,
                      static_cast<unsigned long long>(original.moduleOffset));
        for (auto byte : original.moduleId) { report.append("%02X", byte); }
        report.append("\n");
    } else {
        report.append("unknown original_pointer_relative_offset=unknown\n");
    }
    report.append("original_pointer_classification=%s original_pointer_executable=%s\n"
                  "original_matches_subsdk0_export=%s original_snapshot_valid=%s\n"
                  "reader_quarantined=%s original_reads_matched=%s\n"
                  "original_consistency=two_equal_reads_and_pre_post_checks_not_atomic\n"
#if YW1_COMPANION_EGL_CALLBACK
                  "egl_export_hook_install_attempted=0 got_modification=disabled\n",
#else
                  "egl_hook_install_attempted=0 got_modification=disabled\n",
#endif
                  originalPointerKindName(original.kind), targetBoolean(original.executable),
                  targetBoolean(original.expectedExport), targetBoolean(original.snapshotValid),
                  targetBoolean(original.readerQuarantined), targetBoolean(original.readsMatched));
}
} // 名前空間 yw1::companion
