#include "report.hpp"

#include "diagnostic_target.hpp"

#include <switch.h>

#include <cstring>

namespace yw1::companion {
namespace {

struct ProcessReply {
    NcmProgramLocation location;
    std::uint64_t keys;
    std::uint64_t flags;
};
static_assert(sizeof(ProcessReply) == 32);
static_assert(sizeof(LoaderModuleInfo) == sizeof(ModuleRecord));
static_assert(sizeof(diagnostics::ModuleSnapshot) == kStateSize);

void inspectCheatService(Report& report) {
    Service service{};
    const auto result = report.result("dmnt_init_result", smGetService(&service, "dmnt:cht"));
    if (result != 0) {
        return;
    }
    // 状態照会にも古い接続の整理・toggle保存があり、接続確認以外のcommandは送らない。
    report.append("dmnt_policy=connect_only_no_process_commands\n");
    serviceClose(&service);
    report.append("dmnt_session_close_issued=true\n");
}

class BoundReader {
  public:
    BoundReader(Handle process, const ModuleRecord& module, Report& report)
        : process_(process), module_(module), report_(report) {}

    Result read(std::uint64_t offset, void* output, std::size_t size) {
        if (quarantined_ || output == nullptr || !validRead(module_, offset, size)) {
            return kInvalidData;
        }
        if (!envIsSyscallHinted(0x74) || !envIsSyscallHinted(0x75)) {
            report_.append("memory_read_not_attempted=svc_hint_unavailable\n");
            return kUnavailable;
        }
        const auto source = module_.base + offset;
        const auto page = source & ~std::uint64_t{4095};
        virtmemLock();
        auto* destination = virtmemFindAslr(4096, 4096);
        if (destination == nullptr) {
            virtmemUnlock();
            return kNoAddress;
        }
        const auto mapped = svcMapProcessMemory(destination, process_, page, 4096);
        report_.result("map_process_memory_result", mapped);
        if (mapped != 0) {
            virtmemUnlock();
            return mapped;
        }
        // 対象のpermission/dataは変更しない。aliasにもstoreせず整列wordをacquire読取する。
        const auto* words = reinterpret_cast<const std::uint64_t*>(
            static_cast<const unsigned char*>(destination) + source - page);
        for (std::size_t i = 0; i < size / 8; ++i) {
            const auto value = __atomic_load_n(words + i, __ATOMIC_ACQUIRE);
            std::memcpy(static_cast<unsigned char*>(output) + i * 8, &value, 8);
        }
        const auto unmapped = svcUnmapProcessMemory(destination, process_, page, 4096);
        report_.result("unmap_process_memory_result", unmapped);
        quarantined_ = unmapped != 0;
        virtmemUnlock();
        return unmapped;
    }

    bool quarantined() const {
        return quarantined_;
    }

  private:
    Handle process_;
    ModuleRecord module_;
    Report& report_;
    bool quarantined_ = false;
};

void readBootstrapLog(BoundReader& reader, Report& report, BootstrapLog& output) {
    output.readResult = kExportChanged;
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        diagnostics::ExportedLogHeader before{}, after{};
        auto rc = reader.read(kExportHeaderOffset, &before, sizeof(before));
        report.result("bootstrap_header_read_result", rc);
        if (rc != 0) {
            output.readResult = rc;
            return;
        }
        if (!diagnostics::validExportHeader(before)) {
            output.readResult = kExportInvalid;
            return;
        }
        const auto rounded = (static_cast<std::size_t>(before.used) + 7) & ~std::size_t{7};
        for (std::size_t offset = 0; offset < rounded; offset += 4096) {
            const auto length = rounded - offset < 4096 ? rounded - offset : 4096;
            report.append("bootstrap_page_offset=%zu length=%zu\n", offset, length);
            rc = reader.read(kExportDataOffset + offset, output.text + offset, length);
            report.result("bootstrap_page_read_result", rc);
            if (rc != 0) {
                output.readResult = rc;
                return;
            }
        }
        rc = reader.read(kExportHeaderOffset, &after, sizeof(after));
        report.result("bootstrap_header_reread_result", rc);
        if (rc != 0) {
            output.readResult = rc;
            return;
        }
        if (std::memcmp(&before, &after, sizeof(before)) != 0) {
            report.result("bootstrap_snapshot_changed", kExportChanged);
            continue;
        }
        if (!diagnostics::validExportText(output.text, before.used) ||
            diagnostics::logChecksum(output.text, before.used) != before.checksum) {
            output.readResult = kExportInvalid;
            return;
        }
        output.header = before;
        output.readResult = 0;
        return;
    }
}

void inspectBoundProcess(Report& report, Observation& observation, Handle process,
                         BootstrapLog* bootstrap) {
    const auto pointerBytes = ldrDmntGetServiceSession()->pointer_buffer_size;
    const auto capacity = loaderCapacity(pointerBytes);
    report.append("loader_pointer_buffer_bytes=%u loader_requested_capacity=%zu\n",
                  static_cast<unsigned>(pointerBytes), capacity);
    if (capacity == 0) {
        observation.result = kUnavailable;
        report.append("loader_query_not_attempted=pointer_buffer_too_small_or_unknown\n");
        return;
    }
    LoaderModuleInfo rawModules[kLoaderQueryCapacity]{};
    s32 count = 0;
    auto rc =
        report.result("loader_module_query_result",
                      ldrDmntGetProcessModuleInfo(observation.pid, rawModules, capacity, &count));
    if (rc != 0 || count < 0 || static_cast<std::size_t>(count) > capacity) {
        observation.result = rc != 0 ? rc : kInvalidData;
        return;
    }
    ModuleRecord modules[kLoaderQueryCapacity]{};
    std::memcpy(modules, rawModules, sizeof(modules));
    observation.moduleCount = count;
    report.append("loader_module_count=%d\n", count);
    report.append("loader_list_complete=%s\n",
                  static_cast<std::size_t>(count) < capacity ? "true" : "unknown_capacity_reached");
    for (int i = 0; i < count; ++i) {
        report.append("module[%d].base=0x%016llX size=0x%llX build_id=", i,
                      static_cast<unsigned long long>(modules[i].base),
                      static_cast<unsigned long long>(modules[i].size));
        for (auto byte : modules[i].buildId) {
            report.append("%02X", byte);
        }
        report.append("\n");
    }
    if (findModule(modules, count, diagnostics::kMainBuildId) < 0) {
        observation.result = kWrongTarget;
        report.append("main_build_id_verified=false\n");
        return;
    }
    report.append("main_build_id_verified=true\n");
    const auto index = findModule(modules, count, kExpectedModuleId);
    if (index < 0) {
        const bool absent = index == -1 && static_cast<std::size_t>(count) < capacity;
        observation.moduleDetected = absent ? 0 : -1;
        observation.result = absent ? 0 : kInvalidData;
        return;
    }
    observation.moduleDetected = 1;
    if (process == INVALID_HANDLE) {
        observation.result = kUnavailable;
        return;
    }
    if (!envIsSyscallHinted(0x24) || !envIsSyscallHinted(0x29)) {
        report.append("process_validation_not_attempted=svc_hint_unavailable\n");
        observation.result = kUnavailable;
        return;
    }
    std::uint64_t pid = 0;
    rc = report.result("bound_process_id_result", svcGetProcessId(&pid, process));
    if (rc != 0 || pid != observation.pid) {
        observation.result = rc != 0 ? rc : kWrongTarget;
        return;
    }
    std::uint64_t title = 0;
    rc = report.result("bound_program_id_result",
                       svcGetInfo(&title, InfoType_ProgramId, process, 0));
    if (rc != 0 || title != diagnostics::kTitleId) {
        observation.result = rc != 0 ? rc : kWrongTarget;
        return;
    }
    BoundReader reader(process, modules[index], report);
    diagnostics::ModuleIdentity identity{};
    rc = report.result("module_identity_read_result",
                       reader.read(kIdentityOffset, &identity, sizeof(identity)));
    if (rc == 0) {
        rc = report.result("module_identity_validation_result",
                           diagnostics::validIdentity(identity) ? 0 : kInvalidData);
    }
    if (rc == 0) {
        rc = report.result(
            "diagnostic_memory_read_result",
            reader.read(kStateOffset, &observation.snapshot, sizeof(observation.snapshot)));
    }
    if (rc == 0) {
        rc = report.result("diagnostic_validation_result",
                           diagnostics::validSnapshot(observation.snapshot) ? 0 : kInvalidData);
    }
    observation.result = rc;
    observation.cleanupFailed = reader.quarantined();
    if (rc != 0) {
        return;
    }
    observation.snapshotRead = true;
    const auto init = initialization(observation);
    report.append("exlaunch_init_reached=%d exl_main_reached=%d exlaunch_init_returned=%d\n"
                  "direct_fs_writer_enabled=%d\n",
                  init.exlaunchReached, init.mainReached, init.exlaunchReturned,
                  init.directWriterEnabled);
    observation.dtInitReached = observation.snapshot.results[1] == 0 ? 0 : 1;
    report.append("snapshot_consistency=independent_atomic_words\n");
    report.append("latest_stage=%llu last_result=0x%08X\n",
                  static_cast<unsigned long long>(observation.snapshot.latest >> 32),
                  static_cast<unsigned>(observation.snapshot.latest));
    report.append("first_nonzero_stage_result=0x%016llX\n",
                  static_cast<unsigned long long>(observation.snapshot.firstNonzero));
    report.append("observed_title_id=%016llX directory_index=%u\n",
                  static_cast<unsigned long long>(observation.snapshot.observedTitleId),
                  observation.snapshot.directoryIndex);
    report.append("game_ipc_quarantined=%u\n", observation.snapshot.ipcQuarantined);
    report.append("sm_connect_attempts=%llu\n",
                  static_cast<unsigned long long>(observation.snapshot.smConnectAttempts));
    report.append("game_writer_terminal=%llu\n",
                  static_cast<unsigned long long>(observation.snapshot.writerTerminal));
    for (std::size_t i = 0; i < diagnostics::kStageCount; ++i) {
        if (observation.snapshot.results[i] != 0) {
            report.append(
                "stage[%zu].result=0x%08X first_nonzero=0x%016llX\n", i,
                static_cast<unsigned>(observation.snapshot.results[i]),
                static_cast<unsigned long long>(observation.snapshot.firstNonzeroByStage[i]));
        }
    }
    if (bootstrap != nullptr) {
        bootstrap->pid = observation.pid;
        bootstrap->moduleBase = modules[index].base;
        readBootstrapLog(reader, report, *bootstrap);
        observation.cleanupFailed = reader.quarantined();
        report.result("bootstrap_export_read_result", bootstrap->readResult);
        if (bootstrap->readResult == 0) {
            report.append("bootstrap_used=%u dropped=%u flags=%u generation=%llu\n",
                          bootstrap->header.used, bootstrap->header.dropped,
                          bootstrap->header.flags,
                          static_cast<unsigned long long>(bootstrap->header.generation));
        }
    }
}

} // 無名名前空間

Observation inspectTarget(Report& report, BootstrapLog* bootstrap) {
    Observation observation;
    const auto pm = report.result("pm_init_result", pmdmntInitialize());
    if (pm != 0) {
        observation.result = pm;
        return observation;
    }
    auto rc =
        report.result("application_pid_result", pmdmntGetApplicationProcessId(&observation.pid));
    std::uint64_t title = 0;
    if (rc == 0) {
        rc = report.result("application_title_result", pmdmntGetProgramId(&title, observation.pid));
    }
    report.append("application_pid=%llu application_title=%016llX\n",
                  static_cast<unsigned long long>(observation.pid),
                  static_cast<unsigned long long>(title));
    if (rc != 0 || title != diagnostics::kTitleId || observation.pid == 0) {
        observation.result = rc != 0 ? rc : kWrongTarget;
        pmdmntExit();
        return observation;
    }
    inspectCheatService(report);
    ProcessReply reply{};
    Handle process = INVALID_HANDLE;
    rc = report.result("process_handle_result",
                       serviceDispatchInOut(pmdmntGetServiceSession(), 65000, observation.pid,
                                            reply, .out_handle_attrs = {SfOutHandleAttr_HipcCopy},
                                            .out_handles = &process));
    const bool ownsProcess = rc == 0 && process != INVALID_HANDLE;
    const bool validProcess = ownsProcess && reply.location.program_id == diagnostics::kTitleId;
    if (validProcess) {
        report.append("override_keys=0x%016llX override_flags=0x%016llX\n",
                      static_cast<unsigned long long>(reply.keys),
                      static_cast<unsigned long long>(reply.flags));
        // 固定PMのApplication group(1)/SessionCount(4)。process個別の使用数とは区別する。
        constexpr std::uint64_t resourceInput = 1ULL | (4ULL << 32);
        struct {
            std::int64_t current;
            std::int64_t limit;
        } values{};
        const auto resourceResult = report.result(
            "application_group_session_query_result",
            serviceDispatchInOut(pmdmntGetServiceSession(), 65001, resourceInput, values));
        if (resourceResult == 0) {
            report.append(
                "resource_scope=pm_application_group session_current=%lld session_limit=%lld\n",
                static_cast<long long>(values.current), static_cast<long long>(values.limit));
        }
    }
    rc = report.result("loader_init_result", ldrDmntInitialize());
    if (rc == 0) {
        inspectBoundProcess(report, observation, validProcess ? process : INVALID_HANDLE,
                            bootstrap);
        ldrDmntExit();
        report.append("loader_exit_issued=true\n");
    } else {
        observation.result = rc;
    }
    report.append("module_detected=%d dt_init_reached=%d inspection_result=0x%08X\n",
                  observation.moduleDetected, observation.dtInitReached, observation.result);
    if (ownsProcess) {
        report.result("process_handle_close_result", svcCloseHandle(process));
    }
    pmdmntExit();
    report.append("pm_exit_issued=true\n");
    return observation;
}

} // 名前空間 yw1::companion
