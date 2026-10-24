#include "report.hpp"

namespace yw1::companion {
namespace {
constexpr std::uint32_t kKnownAttributes = 0x1F;

void appendRelative(Report& report, const char* label, std::uint64_t address, std::uint64_t base) {
    const auto relative = relativeAddress(address, base);
    report.append("%s=%s0x%llX\n", label, relative.negative ? "-" : "+",
                  static_cast<unsigned long long>(relative.magnitude));
}
} // 無名名前空間

RelativeAddress relativeAddress(std::uint64_t address, std::uint64_t base) {
    return address < base ? RelativeAddress{true, base - address}
                          : RelativeAddress{false, address - base};
}

bool validTargetModules(std::span<const ModuleRecord> modules) {
    if (modules.empty() || modules.size() > kLoaderQueryCapacity) {
        return false;
    }
    for (std::size_t i = 0; i < modules.size(); ++i) {
        const auto& module = modules[i];
        if (module.base == 0 || module.base % 4096 != 0 || module.size == 0 ||
            module.size % 4096 != 0 || module.base > UINT64_MAX - module.size) {
            return false;
        }
        for (std::size_t j = 0; j < i; ++j) {
            const auto& previous = modules[j];
            if (equalId(module.buildId, previous.buildId) ||
                (module.base < previous.base + previous.size &&
                 previous.base < module.base + module.size)) {
                return false;
            }
        }
    }
    return true;
}

bool validTargetAddress(std::uint64_t address, std::uint64_t base, std::uint64_t size) {
    return address != 0 && base % 4096 == 0 && size != 0 && size % 4096 == 0 &&
           base <= UINT64_MAX - size && address >= base && address - base < size;
}

const char* targetMemoryTypeName(std::uint32_t type) {
    constexpr const char* names[]{"Unmapped",         "Io",
                                  "Normal",           "CodeStatic",
                                  "CodeMutable",      "Heap",
                                  "SharedMem",        "WeirdMappedMem",
                                  "ModuleCodeStatic", "ModuleCodeMutable",
                                  "IpcBuffer0",       "MappedMemory",
                                  "ThreadLocal",      "TransferMemIsolated",
                                  "TransferMem",      "ProcessMem",
                                  "Reserved",         "IpcBuffer1",
                                  "IpcBuffer3",       "KernelStack",
                                  "CodeReadOnly",     "CodeWritable",
                                  "Coverage",         "Insecure"};
    return type < std::size(names) ? names[type] : "unknown";
}

void validateTargetRegion(std::uint64_t address, TargetMemoryObservation& output) {
    const auto& region = output.region;
    output.regionValid = false;
    output.containsPointer = output.mapped = output.readable = output.writable = output.executable =
        -1;
    output.typeKnown = region.type <= 0x17;
    output.attributesKnown = (region.attributes & ~kKnownAttributes) == 0;
    output.permissionKnown = (region.permission & ~7u) == 0;
    output.validationResult = kInvalidData;
    if (region.base % 4096 != 0 || region.size == 0 || region.size % 4096 != 0 ||
        region.base > UINT64_MAX - region.size) {
        return;
    }
    output.containsPointer = address >= region.base && address - region.base < region.size ? 1 : 0;
    if (output.containsPointer == 0) {
        return;
    }
    output.regionValid = true;
    output.validationResult = 0;
    if (!output.typeKnown) {
        return;
    }
    // Unmapped/Reservedは空間の記述であり、物理メモリの存在を示さない。
    if (region.type == 0 || region.type == 0x10) {
        output.mapped = 0;
        if (region.permission == 0) {
            output.readable = output.writable = output.executable = 0;
        }
        return;
    }
    // Coverageは導入SDKでも未提供。未知属性・permissionも実効RX/RWと解釈しない。
    if (region.type == 0x16) {
        return;
    }
    output.mapped = 1;
    if (!output.attributesKnown || !output.permissionKnown) {
        return;
    }
    output.readable = (region.permission & 1) != 0 ? 1 : 0;
    output.writable = (region.permission & 2) != 0 ? 1 : 0;
    output.executable = (region.permission & 4) != 0 ? 1 : 0;
}

const char* targetBoolean(int value) {
    return value < 0 ? "unknown" : (value ? "true" : "false");
}

void appendTargetMemoryReport(Report& report, const Observation& observation) {
    const auto& got = observation.got;
    const auto& memory = got.memory;
    report.append("\n=== YW1 EGL GOT Target Memory %s ===\n"
                  "source=companion-nro mode=read-only memory_scope=bound_game_process\n"
                  "observation_stage=album_snapshot target_query_attempted=%u\n",
                  kCompanionVersion, memory.queryAttempted);
    report.result("target_address_space_base_result", memory.addressBaseResult);
    report.result("target_address_space_size_result", memory.addressSizeResult);
    report.result("target_query_result", memory.queryResult);
    report.result("target_validation_result", memory.validationResult);
    report.append("target_address_bounds_scope=game_aslr_alias_code_region_not_entire_process\n");
    if (memory.addressBaseResult == 0 && memory.addressSizeResult == 0) {
        report.append("target_address_space_base=0x%016llX target_address_space_size=0x%llX\n",
                      static_cast<unsigned long long>(memory.addressSpaceBase),
                      static_cast<unsigned long long>(memory.addressSpaceSize));
    } else {
        report.append("target_address_space_base=unknown target_address_space_size=unknown\n");
    }
    if (got.valueKnown) {
        report.append("target_address=0x%016llX\n", static_cast<unsigned long long>(got.slotValue));
        appendRelative(report, "target_relative_to_main", got.slotValue, got.mainBase);
        report.append("target_instruction_aligned=%s\n",
                      targetBoolean(got.slotValue % 4 == 0 ? 1 : 0));
    } else {
        report.append("target_address=unknown target_relative_to_main=unknown\n"
                      "target_instruction_aligned=unknown\n");
    }
    if (memory.infoReturned) {
        const auto& region = memory.region;
        report.append(
            "target_region_base=0x%016llX target_region_size=0x%llX\n"
            "target_memory_type=0x%08X target_memory_type_name=%s\n"
            "target_memory_attributes=0x%08X target_memory_permission=0x%08X\n"
            "target_ipc_refcount=%u target_device_refcount=%u\n"
            "target_type_known=%s target_attributes_known=%s target_permission_known=%s\n",
            static_cast<unsigned long long>(region.base),
            static_cast<unsigned long long>(region.size), region.type,
            targetMemoryTypeName(region.type), region.attributes, region.permission,
            region.ipcRefcount, region.deviceRefcount, targetBoolean(memory.typeKnown),
            targetBoolean(memory.attributesKnown), targetBoolean(memory.permissionKnown));
        appendRelative(report, "target_region_relative_to_main", region.base, got.mainBase);
    } else {
        report.append("target_region_base=unknown target_region_size=unknown\n"
                      "target_region_relative_to_main=unknown target_memory_type=unknown\n"
                      "target_memory_attributes=unknown target_memory_permission=unknown\n"
                      "target_ipc_refcount=unknown target_device_refcount=unknown\n");
    }
    report.append("target_region_valid=%s target_region_contains_pointer=%s target_mapped=%s\n"
                  "target_readable=%s target_writable=%s target_executable=%s\n"
                  "memory_state_semantics=MemoryInfo_type_only_not_full_kernel_state\n",
                  targetBoolean(memory.infoReturned ? (memory.regionValid ? 1 : 0) : -1),
                  targetBoolean(memory.containsPointer), targetBoolean(memory.mapped),
                  targetBoolean(memory.readable), targetBoolean(memory.writable),
                  targetBoolean(memory.executable));
#if YW1_COMPANION_EGL_CODE_SNAPSHOT
    const auto& snapshot = got.snapshot;
    report.append("target_code_read_attempted=%u target_code_read_result=0x%08X\n"
                  "target_code_bytes=",
                  snapshot.readAttempted, snapshot.readAttempted ? snapshot.result : UINT32_MAX);
    if (snapshot.bytesValid) {
        for (auto byte : snapshot.bytes) {
            report.append("%02X", byte);
        }
    } else {
        report.append("unknown");
    }
    report.append(" target_code_length=%u\ntarget_code_read_policy=bounded_16_byte_snapshot\n",
                  snapshot.bytesValid ? kSnapshotLength : 0);
#else
    report.append(
        "target_code_read_attempted=0 target_code_read_result=0xFFFFFFFF\n"
        "target_code_bytes=unknown target_code_length=0\n"
        "target_code_read_policy=disabled_pending_region_and_mapping_lifetime_evidence\n");
#endif
    report.append("got_hook_install_attempted=0 got_modification=disabled\n");
}
} // 名前空間 yw1::companion
