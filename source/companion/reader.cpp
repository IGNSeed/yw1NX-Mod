#include "report.hpp"

#include "diagnostic_target.hpp"
#if YW1_COMPANION_EGL_CALLBACK
#include "legacy_diagnostic_target.hpp"
#if YW1_EGL_CONTEXT_PROBE
#include "B028A_diagnostic_target.hpp"
#include "B028B_diagnostic_target.hpp"
#endif
#endif

#include <switch.h>

#include <cstring>
#include <type_traits>

namespace yw1::companion {
namespace {
#if YW1_EGL_CONTEXT_PROBE
struct CallbackLayout {
    const std::uint8_t* id;
    std::uint64_t identity, state, header, data, egl, callback;
};
constexpr CallbackLayout kB028Layouts[]{
    {kB028AExpectedModuleId, kB028AIdentityOffset, kB028AStateOffset, kB028AExportHeaderOffset,
     kB028AExportDataOffset, kB028AEglOffset, kB028ACallbackOffset},
    {kB028BExpectedModuleId, kB028BIdentityOffset, kB028BStateOffset, kB028BExportHeaderOffset,
     kB028BExportDataOffset, kB028BEglOffset, kB028BCallbackOffset}};
#endif

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
    BoundReader(Handle process, const ModuleRecord& module, Report& report,
                [[maybe_unused]] bool legacy = false
#if YW1_EGL_CONTEXT_PROBE
                , unsigned b028 = 0
#endif
                )
        : process_(process), module_(module), report_(report)
#if YW1_COMPANION_EGL_CALLBACK
          , legacy_(legacy)
#endif
#if YW1_EGL_CONTEXT_PROBE
          , b028_(b028)
#endif
          {}

    Result read(std::uint64_t offset, void* output, std::size_t size) {
#if YW1_EGL_CONTEXT_PROBE
        if (b028_) {
            if (b028_ > 2) { return kInvalidData; }
            const auto& layout = kB028Layouts[b028_ - 1];
            if (offset == kIdentityOffset) { offset = layout.identity; }
            else if (offset == kStateOffset) { offset = layout.state; }
            else if (offset == kExportHeaderOffset) { offset = layout.header; }
            else if (offset == kEglOffset) { offset = layout.egl; }
            else if (offset == kCallbackOffset) { offset = layout.callback; }
            else if (offset >= kExportDataOffset &&
                     offset - kExportDataOffset < diagnostics::kExportCapacity) {
                offset = layout.data + offset - kExportDataOffset;
            } else { return kInvalidData; }
        }
#endif
#if YW1_COMPANION_EGL_CALLBACK
        if (legacy_) {
            if (offset == kIdentityOffset) { offset = kLegacyIdentityOffset; }
            else if (offset == kStateOffset) { offset = kLegacyStateOffset; }
            else if (offset == kExportHeaderOffset) { offset = kLegacyExportHeaderOffset; }
            else if (offset == kEglOffset) { offset = kLegacyEglOffset; }
            else if (offset >= kExportDataOffset &&
                     offset - kExportDataOffset < diagnostics::kExportCapacity) {
                offset = kLegacyExportDataOffset + offset - kExportDataOffset;
            } else { return kInvalidData; }
        }
#endif
        return readFrom(module_, offset, output, size);
    }
#if YW1_COMPANION_EGL_GOT_PROBE
    Result readGot(const ModuleRecord& main, std::uint64_t& output) {
        if (!equalId(main.buildId, diagnostics::kMainBuildId)) {
            return kWrongTarget;
        }
        return readFrom(main, kGotSlotOffset, &output, 8);
    }
#endif
#if YW1_COMPANION_EGL_CODE_SNAPSHOT
    Result readSnapshot(const GotObservation& got, CodeSnapshot& output) {
        CodeSnapshot checked;
        if (quarantined_ || snapshotPreflight(got, checked) != 0) {
            return kInvalidData;
        }
        if (!envIsSyscallHinted(0x74) || !envIsSyscallHinted(0x75)) {
            return kUnavailable;
        }
        virtmemLock();
        auto* destination = virtmemFindAslr(4096, 4096);
        if (destination == nullptr) {
            virtmemUnlock();
            return kNoAddress;
        }
        output.mapAttempted = true;
        output.map = svcMapProcessMemory(destination, process_, checked.pageAddress, 4096);
        if (output.map != 0) {
            virtmemUnlock();
            return output.map;
        }
        // Kernelが保持したphysical pageのaliasだけを読む。source/aliasへのstoreは行わない。
        const auto* words = reinterpret_cast<const std::uint32_t*>(
            static_cast<const unsigned char*>(destination) + checked.pageOffset);
        std::array<std::uint32_t, kSnapshotLength / 4> first{}, second{};
        output.readAttempted = true;
        for (std::size_t i = 0; i < first.size(); ++i) {
            first[i] = __atomic_load_n(words + i, __ATOMIC_ACQUIRE);
        }
        for (std::size_t i = 0; i < second.size(); ++i) {
            second[i] = __atomic_load_n(words + i, __ATOMIC_ACQUIRE);
        }
        output.readsMatched = first == second;
        output.read = output.readsMatched ? 0 : kSnapshotMismatch;
        // 不一致でも必ずUnmapする。失敗したaliasは再利用せず、既存Reader全体を隔離する。
        output.unmapAttempted = true;
        output.unmap = svcUnmapProcessMemory(destination, process_, checked.pageAddress, 4096);
        quarantined_ = output.unmap != 0;
        output.cleanupQuarantined = quarantined_;
        virtmemUnlock();
        if (output.unmap != 0) {
            return output.unmap;
        }
        if (output.read != 0) {
            return output.read;
        }
        std::memcpy(output.bytes.data(), first.data(), kSnapshotLength);
        return 0;
    }
#endif
#if YW1_COMPANION_SALTY_ORIGINAL_EGL
    Result readOriginal(const GotObservation& got, OriginalEglObservation& output) {
        if (quarantined_) {
            return kSessionClosed;
        }
        if (originalStoragePreflight(got, output) != 0) {
            return output.preflight;
        }
        if (!envIsSyscallHinted(0x74) || !envIsSyscallHinted(0x75)) {
            return kUnavailable;
        }
        virtmemLock();
        auto* destination = virtmemFindAslr(4096, 4096);
        if (destination == nullptr) {
            virtmemUnlock();
            return kNoAddress;
        }
        output.mapAttempted = true;
        output.map = svcMapProcessMemory(destination, process_, output.pageAddress, 4096);
        if (output.map != 0) {
            virtmemUnlock();
            return output.map;
        }
        // 固定保存先の整列8 byteだけを二度読む。source/aliasには書き込まない。
        const auto* value = reinterpret_cast<const std::uint64_t*>(
            static_cast<const unsigned char*>(destination) + output.pageOffset);
        output.readAttempted = true;
        const auto first = __atomic_load_n(value, __ATOMIC_ACQUIRE);
        const auto second = __atomic_load_n(value, __ATOMIC_ACQUIRE);
        output.readsMatched = first == second;
        output.read = output.readsMatched ? 0 : kSnapshotMismatch;
        output.unmapAttempted = true;
        output.unmap = svcUnmapProcessMemory(destination, process_, output.pageAddress, 4096);
        quarantined_ = output.unmap != 0;
        output.readerQuarantined = quarantined_;
        virtmemUnlock();
        if (output.unmap != 0) {
            return output.unmap;
        }
        if (output.read != 0) {
            return output.read;
        }
        output.pointer = first;
        return 0;
    }
#endif
    bool quarantined() const {
        return quarantined_;
    }

  private:
    Result readFrom(const ModuleRecord& module, std::uint64_t offset, void* output,
                    std::size_t size) {
        if (quarantined_ || output == nullptr || !validRead(module, offset, size)) {
            return kInvalidData;
        }
        if (!envIsSyscallHinted(0x74) || !envIsSyscallHinted(0x75)) {
            report_.append("memory_read_not_attempted=svc_hint_unavailable\n");
            return kUnavailable;
        }
        const auto source = module.base + offset;
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

    Handle process_;
    ModuleRecord module_;
    Report& report_;
    bool quarantined_ = false;
#if YW1_COMPANION_EGL_CALLBACK
    bool legacy_ = false;
#endif
#if YW1_EGL_CONTEXT_PROBE
    unsigned b028_ = 0;
#endif
};

#if YW1_COMPANION_EGL_GOT_PROBE
static_assert(sizeof(MemoryInfo) == 40 && offsetof(MemoryInfo, perm) == 24);
#if YW1_COMPANION_EGL_TARGET_MEMORY
static_assert(offsetof(MemoryInfo, addr) == 0 && offsetof(MemoryInfo, size) == 8 &&
              offsetof(MemoryInfo, type) == 16 && offsetof(MemoryInfo, attr) == 20 &&
              offsetof(MemoryInfo, ipc_refcount) == 28 &&
              offsetof(MemoryInfo, device_refcount) == 32);
static_assert(MemType_Unmapped == 0 && MemType_Insecure == 0x17 &&
              MemAttr_IsPermissionLocked == 0x10 && Perm_R == 1 && Perm_W == 2 && Perm_X == 4);

void inspectTargetMemory(Handle process, std::span<const ModuleRecord> modules,
                         GotObservation& output) {
    auto& memory = output.memory;
    // 呼出し元はMain完全ID・PID/Title・handleを検証し、GOT読取とUnmapに成功した後だけ。
    if (!validTargetModules(modules) || output.slotValue == 0) {
        memory.validationResult = kInvalidData;
        return;
    }
    if (!envIsSyscallHinted(0x76) || !envIsSyscallHinted(0x29)) {
        memory.validationResult = kUnavailable;
        return;
    }
    // 固定39bit等を仮定せず、同じgame handleからASLR対象領域の範囲を取得する。
    memory.addressBaseResult =
        svcGetInfo(&memory.addressSpaceBase, InfoType_AslrRegionAddress, process, 0);
    memory.addressSizeResult =
        svcGetInfo(&memory.addressSpaceSize, InfoType_AslrRegionSize, process, 0);
    if (memory.addressBaseResult != 0 || memory.addressSizeResult != 0) {
        memory.validationResult =
            memory.addressBaseResult != 0 ? memory.addressBaseResult : memory.addressSizeResult;
        return;
    }
    if (!validTargetAddress(output.slotValue, memory.addressSpaceBase, memory.addressSpaceSize)) {
        memory.validationResult = kInvalidData;
        return;
    }
    MemoryInfo info{};
    u32 pageInfo = 0;
    memory.queryAttempted = true;
    memory.queryResult = svcQueryProcessMemory(&info, &pageInfo, process, output.slotValue);
    output.targetQueryResult = memory.queryResult;
    if (memory.queryResult != 0) {
        return;
    }
    memory.infoReturned = true;
    memory.region = {info.addr, info.size,         info.type,           info.attr,
                     info.perm, info.ipc_refcount, info.device_refcount};
    validateTargetRegion(output.slotValue, memory);
    output.executable = memory.executable;
    if (memory.regionValid && memory.permissionKnown) {
        output.targetPermission = info.perm;
    }
    if (output.executable == 0) {
        output.binding = GotBinding::NonExecutable;
    } else if (output.executable == 1 && output.binding == GotBinding::Unknown &&
               output.slotValue % 4 == 0) {
        output.binding = GotBinding::ExecutableCandidate;
    }
    // 既知module外は、X bitが判明しても所有者や関数としての正当性を確定しない。
}
#if YW1_COMPANION_EGL_CODE_SNAPSHOT
Result revalidateSnapshot(BoundReader& reader, Handle process, const ModuleRecord& main,
                          std::uint64_t expectedPid, std::uint64_t expectedTitle,
                          const GotObservation& got, CodeSnapshot& output, bool before,
                          std::span<const ModuleRecord> expectedModules = {}) {
    auto& pidResult = before ? output.beforePid : output.afterPid;
    auto& titleResult = before ? output.beforeTitle : output.afterTitle;
    auto& gotResult = before ? output.beforeGot : output.afterGot;
    auto& queryResult = before ? output.beforeQuery : output.afterQuery;
    if (reader.quarantined()) {
        return kSessionClosed;
    }
    std::uint64_t pid = 0, title = 0, value = 0;
    pidResult = svcGetProcessId(&pid, process);
    if (pidResult != 0) {
        return pidResult;
    }
    if (pid != expectedPid) {
        return kWrongTarget;
    }
    titleResult = svcGetInfo(&title, InfoType_ProgramId, process, 0);
    if (titleResult != 0) {
        return titleResult;
    }
    if (title != expectedTitle) {
        return kWrongTarget;
    }
#if YW1_COMPANION_EGL_CODE_ATTRIBUTION
    auto& mainResult = before ? output.beforeMain : output.afterMain;
    // 同じPIDのLoader記録を毎回取り直し、完全ID・base・sizeを初回のMainと照合する。
    const auto capacity = loaderCapacity(ldrDmntGetServiceSession()->pointer_buffer_size);
    if (capacity == 0) {
        return mainResult = kUnavailable;
    }
    LoaderModuleInfo raw[kLoaderQueryCapacity]{};
    s32 count = 0;
    mainResult = ldrDmntGetProcessModuleInfo(expectedPid, raw, static_cast<int>(capacity), &count);
    if (mainResult != 0) {
        return mainResult;
    }
    if (count <= 0 || static_cast<std::size_t>(count) >= capacity) {
        return mainResult = kInvalidData;
    }
    ModuleRecord modules[kLoaderQueryCapacity]{};
    std::memcpy(modules, raw, sizeof(modules));
    const auto index = findModule(modules, count, diagnostics::kMainBuildId);
    if (index < 0) {
        return mainResult = index == -1 ? kWrongTarget : kInvalidData;
    }
    if (modules[index].base != main.base || modules[index].size != main.size) {
        return mainResult = kSnapshotChanged;
    }
    if (!expectedModules.empty()) {
        if (static_cast<std::size_t>(count) != expectedModules.size() ||
            !validTargetModules({modules, static_cast<std::size_t>(count)})) {
            return mainResult = kSnapshotChanged;
        }
        for (std::size_t i = 0; i < expectedModules.size(); ++i) {
            if (modules[i].base != expectedModules[i].base ||
                modules[i].size != expectedModules[i].size ||
                !equalId(modules[i].buildId, expectedModules[i].buildId)) {
                return mainResult = kSnapshotChanged;
            }
        }
    }
#endif
    gotResult = reader.readGot(main, value);
    if (gotResult != 0) {
        return gotResult;
    }
    if (value != got.slotValue) {
        return kSnapshotChanged;
    }
    MemoryInfo info{};
    u32 pageInfo = 0;
    queryResult = svcQueryProcessMemory(&info, &pageInfo, process, got.slotValue);
    if (queryResult != 0) {
        return queryResult;
    }
    TargetMemoryObservation memory;
    memory.queryAttempted = memory.infoReturned = true;
    memory.queryResult = 0;
    memory.addressBaseResult = memory.addressSizeResult = 0;
    memory.addressSpaceBase = got.memory.addressSpaceBase;
    memory.addressSpaceSize = got.memory.addressSpaceSize;
    memory.region = {info.addr, info.size,         info.type,           info.attr,
                     info.perm, info.ipc_refcount, info.device_refcount};
    validateTargetRegion(got.slotValue, memory);
    if (!sameSnapshotRegion(got.memory.region, memory.region)) {
        return kSnapshotChanged;
    }
    auto current = got;
    current.memory = memory;
    CodeSnapshot checked;
    return snapshotPreflight(current, checked);
}

void inspectCodeSnapshot(BoundReader& reader, Handle process, const ModuleRecord& main,
                         std::uint64_t pid, std::uint64_t title, GotObservation& got) {
    auto& output = got.snapshot;
    output.result = snapshotPreflight(got, output);
    if (output.result != 0) {
        return;
    }
    for (const auto svc : {0x24, 0x29, 0x74, 0x75, 0x76}) {
        if (!envIsSyscallHinted(svc)) {
            output.result = output.preflight = kUnavailable;
            return;
        }
    }
    output.revalidation = revalidateSnapshot(reader, process, main, pid, title, got, output, true);
    output.result = output.revalidation;
    output.cleanupQuarantined = reader.quarantined();
    if (output.result != 0) {
        return;
    }
    output.result = reader.readSnapshot(got, output);
    if (output.result != 0) {
        return;
    }
    output.postvalidation =
        revalidateSnapshot(reader, process, main, pid, title, got, output, false);
    output.result = output.postvalidation;
    output.cleanupQuarantined = reader.quarantined();
    output.bytesValid = output.result == 0;
    if (!output.bytesValid) {
        output.bytes.fill(0);
    }
}
#if YW1_COMPANION_SALTY_ORIGINAL_EGL
Result queryOriginalRegion(Handle process, std::uint64_t address,
                           const TargetMemoryObservation& bounds, TargetMemoryObservation& out) {
    out.addressBaseResult = bounds.addressBaseResult;
    out.addressSizeResult = bounds.addressSizeResult;
    out.addressSpaceBase = bounds.addressSpaceBase;
    out.addressSpaceSize = bounds.addressSpaceSize;
    MemoryInfo info{};
    u32 pageInfo = 0;
    out.queryAttempted = true;
    out.queryResult = svcQueryProcessMemory(&info, &pageInfo, process, address);
    if (out.queryResult != 0) {
        return out.queryResult;
    }
    out.infoReturned = true;
    out.region = {info.addr, info.size, info.type, info.attr, info.perm,
                  info.ipc_refcount, info.device_refcount};
    validateTargetRegion(address, out);
    return out.validationResult;
}

Result recheckOriginalIdentity(BoundReader& reader, Handle process,
                               std::span<const ModuleRecord> modules, const ModuleRecord& main,
                               std::uint64_t pid, std::uint64_t title, const GotObservation& got) {
    CodeSnapshot current;
    auto result = revalidateSnapshot(reader, process, main, pid, title, got, current, true, modules);
    if (result != 0) {
        return result;
    }
    result = reader.readSnapshot(got, current);
    if (result != 0) {
        return result;
    }
    // 前後で現在の64 byteも照合する。完全なイメージ同一性や原子性は主張しない。
    return current.bytes == got.snapshot.bytes ? 0 : kOriginalIdentityMismatch;
}

Result recheckOriginalStorage(Handle process, const GotObservation& got,
                              const OriginalEglObservation& expected) {
    auto current = expected;
    current.storage = {};
    const auto result = queryOriginalRegion(process, current.storageAddress, got.memory,
                                             current.storage);
    if (result != 0) {
        return result;
    }
    if (!sameSnapshotRegion(current.storage.region, expected.storage.region)) {
        return kSnapshotChanged;
    }
    return originalStoragePreflight(got, current);
}

void inspectOriginal(BoundReader& reader, Handle process, std::span<const ModuleRecord> modules,
                     const ModuleRecord& main, std::uint64_t pid, std::uint64_t title,
                     GotObservation& got) {
    auto& out = got.original;
    const auto inspect = [&]() -> Result {
        if (reader.quarantined()) {
            return kSessionClosed;
        }
        std::array<std::uint8_t, 32> digest{};
        if (got.snapshot.bytesValid && got.snapshot.result == 0) {
            sha256CalculateHash(digest.data(), got.snapshot.bytes.data(), kSnapshotLength);
        }
        auto result = deriveOriginalStorage(got, digest, out);
        if (result != 0) {
            return result;
        }
        out.storageQueryAttempted = true;
        result = queryOriginalRegion(process, out.storageAddress, got.memory, out.storage);
        out.storageInfoReturned = out.storage.infoReturned;
        if (result != 0) {
            return result;
        }
        result = originalStoragePreflight(got, out);
        if (result != 0) {
            return result;
        }
        out.beforeValidation = recheckOriginalIdentity(reader, process, modules, main, pid, title, got);
        if (out.beforeValidation != 0) {
            return out.beforeValidation;
        }
        out.beforeStorage = recheckOriginalStorage(process, got, out);
        if (out.beforeStorage != 0) {
            return out.beforeStorage;
        }
        result = reader.readOriginal(got, out);
        if (result != 0) {
            return result;
        }
        out.afterStorage = recheckOriginalStorage(process, got, out);
        if (out.afterStorage != 0) {
            return out.afterStorage;
        }
        out.postvalidation = recheckOriginalIdentity(reader, process, modules, main, pid, title, got);
        if (out.postvalidation != 0) {
            return out.postvalidation;
        }
        out.snapshotValid = true;
        // 得られた値の参照先は属性だけを照会する。Map・命令読取・呼出しは行わない。
        if (out.pointer != 0 && out.pointer % 4 == 0 &&
            validTargetAddress(out.pointer, got.memory.addressSpaceBase, got.memory.addressSpaceSize)) {
            out.pointerQueryAttempted = true;
            queryOriginalRegion(process, out.pointer, got.memory, out.target);
            out.pointerQuery = out.target.queryResult;
            out.pointerInfoReturned = out.target.infoReturned;
        }
        classifyOriginalPointer(modules, got, out);
        return 0;
    };
    out.result = inspect();
    out.readerQuarantined = reader.quarantined();
    if (!out.snapshotValid) {
        out.pointer = 0;
    }
}
#endif
#endif
#endif
bool validGameInfo(const MemoryInfo& info, std::uint64_t address, std::size_t size) {
    return info.addr % 4096 == 0 && info.size % 4096 == 0 && (info.perm & ~7u) == 0 &&
           diagnostics::contains(info.addr, info.size, address, size);
}
void inspectGot(BoundReader& reader, Handle process, std::span<const ModuleRecord> modules,
                bool complete, std::uint64_t pid, std::uint64_t title, GotObservation& output) {
    output.result = validateGotRead(pid, title, modules, complete, output);
    if (output.result != 0) {
        return;
    }
    if (reader.quarantined()) {
        output.result = kSessionClosed;
        return;
    }
    const auto main = findModule(modules.data(), modules.size(), diagnostics::kMainBuildId);
    if (envIsSyscallHinted(0x76)) {
        MemoryInfo info{};
        u32 pageInfo = 0;
        // QueryMemory(current process)ではなく、検証済みgame handleを明示する。
        output.pageQueryResult =
            svcQueryProcessMemory(&info, &pageInfo, process, output.slotAddress);
        if (output.pageQueryResult == 0) {
            if (!validGameInfo(info, output.slotAddress & ~std::uint64_t{4095}, 4096)) {
                output.result = kInvalidData;
                return;
            }
            output.pagePermission = info.perm;
            if ((info.perm & 1) == 0) {
                output.result = 0xE108;
                return;
            }
        }
    }
    std::uint64_t value = 0;
    output.attempted = true;
    output.readResult = reader.readGot(modules[main], value);
    output.result = output.readResult;
    if (output.result != 0) {
        return;
    }
    output.slotValue = value;
    output.valueKnown = true;
    classifyGot(modules, output);
#if YW1_COMPANION_EGL_TARGET_MEMORY
    inspectTargetMemory(process, modules, output);
#if YW1_COMPANION_EGL_CODE_SNAPSHOT
    inspectCodeSnapshot(reader, process, modules[main], pid, title, output);
#if YW1_COMPANION_SALTY_ORIGINAL_EGL
    inspectOriginal(reader, process, modules, modules[main], pid, title, output);
#endif
#endif
#else
    if (output.normalizedKnown && envIsSyscallHinted(0x76)) {
        MemoryInfo info{};
        u32 pageInfo = 0;
        // 未知pointerの逆参照や命令実行はしない。module内と判明したpointerの属性照会だけ。
        output.targetQueryResult =
            svcQueryProcessMemory(&info, &pageInfo, process, output.slotValue);
        if (output.targetQueryResult == 0 && validGameInfo(info, output.slotValue, 1)) {
            output.targetPermission = info.perm;
            output.executable = (info.perm & 4) != 0 ? 1 : 0;
            if (output.executable == 0) {
                output.binding = GotBinding::NonExecutable;
            } else if (output.binding == GotBinding::Unknown && output.slotValue % 4 == 0) {
                output.binding = GotBinding::ExecutableCandidate;
            }
        }
    }
#endif
}
#endif

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
    auto index = findModule(modules, count, kExpectedModuleId);
#if YW1_COMPANION_EGL_CALLBACK
    const auto legacyIndex = findModule(modules, count, kLegacyExpectedModuleId);
#if YW1_EGL_CONTEXT_PROBE
    int oldIndex = -1;
    for (unsigned i = 0; i < 2; ++i) {
        const auto found = findModule(modules, count, kB028Layouts[i].id);
        if (found < -1 ||
            (found >= 0 && (oldIndex >= 0 || index != -1 || legacyIndex != -1))) {
            observation.result = kInvalidData;
            report.append("diagnostic_module_selection=ambiguous_B028_or_current\n");
            return;
        }
        if (found >= 0) { oldIndex = found; observation.b028Profile = i + 1; }
    }
    if (oldIndex >= 0) { index = oldIndex; }
#endif
    if (index < -1 || legacyIndex < -1) {
        observation.result = kInvalidData;
        return;
    }
    if (index >= 0 && legacyIndex >= 0) {
        observation.result = kInvalidData;
        report.append("diagnostic_module_selection=ambiguous_current_and_legacy\n");
        return;
    }
    if (index == -1 && legacyIndex >= 0) {
        index = legacyIndex;
        observation.legacyModule = true;
    }
#endif
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
    BoundReader reader(process, modules[index], report
#if YW1_COMPANION_EGL_CALLBACK
                       , observation.legacyModule
#endif
#if YW1_EGL_CONTEXT_PROBE
                       , observation.b028Profile
#endif
    );
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
#if YW1_BOOTSTRAP_EGL_DIAGNOSTIC
    observation.eglReadResult = kExportChanged;
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        alignas(64) std::uint64_t payload[1 + sizeof(diagnostics::EglDiagnosticWire) / 8]{};
        auto result = reader.read(kEglOffset, payload, sizeof(payload));
        std::uint64_t after = 0;
        if (result == 0) {
            result = reader.read(kEglOffset, &after, 8);
        }
        report.result("egl_wire_map_read_result", result);
        if (result != 0) {
            observation.eglReadResult = result;
            break;
        }
        if (payload[0] == 0 || (payload[0] & 1) != 0 || payload[0] != after) {
            continue;
        }
        std::memcpy(static_cast<void*>(&observation.egl), payload + 1, sizeof(observation.egl));
        observation.eglReadResult = diagnostics::validEglWire(observation.egl) ? 0 : kInvalidData;
        break;
    }
    observation.cleanupFailed = reader.quarantined();
    report.result("egl_diagnostic_read_result", observation.eglReadResult);
    report.append(
        "\n=== YW1 EGL Snapshot %s ===\nsource=module-bootstrap\ntransport=companion-nro\n",
        diagnostics::kDiagnosticVersion);
    if (observation.eglReadResult == 0) {
        const auto& egl = observation.egl;
#if YW1_BOOTSTRAP_SAFE_HOOK_DIAGNOSTIC
        const auto& hook = egl.installer;
        report.append("\n=== YW1 Hook Installer B0.2.1 "
                      "===\nsource=module-bootstrap\ntransport=companion-nro\n"
                      "hook_backend=%u hook_state=%u "
                      "hook_result_namespace=mod_preflight_platform_operations\n"
                      "hook_preflight_result=0x%08X hook_target_identity_result=0x%08X "
                      "hook_target_instruction_result=0x%08X\n"
                      "hook_trampoline_prepare_result=0x%08X hook_original_publish_result=0x%08X\n"
                      "hook_thread_quiescence_verified=%u hook_mapping_result=0x%08X "
                      "hook_cache_sync_result=0x%08X\n"
                      "hook_commit_attempted=%u hook_commit_result=0x%08X "
                      "hook_rollback_attempted=%u hook_rollback_result=0x%08X\n"
                      "hook_active=%u hook_blocked_reason=0x%08X hook_cleanup_result=0x%08X\n"
                      "hook_resources_retained=%u hook_lease_release_result=0x%08X\n",
                      static_cast<unsigned>(hook.backend), static_cast<unsigned>(hook.state),
                      hook.preflight, hook.identity, hook.instructions, hook.trampoline,
                      hook.publish, hook.quiescenceVerified, hook.mapping, hook.cache,
                      hook.commitAttempted, hook.commit, hook.rollbackAttempted, hook.rollback,
                      hook.active, hook.blockedReason, hook.cleanup, hook.resourcesRetained,
                      hook.leaseRelease);
#if YW1_COMPANION_EGL_GOT_PROBE
#if !YW1_COMPANION_EGL_CALLBACK
        report.append("hook_preflight_result_semantics=guard_derived_not_installer_execution\n"
                      "static_guard_executed=%s static_guard_status=0x%08X\n"
                      "guard_derived_preflight_result=0x%08X\n"
                      "installer_preflight_result=0xFFFFFFFF installer_preflight_executed=false\n"
                      "installer_linked_in_module=false\n",
                      egl.guardStatus == UINT32_MAX ? "unknown_unrecorded" : "true",
                      egl.guardStatus, hook.preflight);
#else
        report.append("export_guard_semantics=legacy_export_candidate_guard_only\n"
                      "main_callsite_installer_status=see_callback_canary_section\n");
#endif
#endif
#endif
        report.append(
#if YW1_COMPANION_EGL_CALLBACK
                      "egl_export_hook_install_attempted=%u egl_export_hook_install_result=0x%08X\n"
#else
                      "egl_hook_install_attempted=%u egl_hook_install_result=0x%08X\n"
#endif
                      "egl_hook_guard_status=%u egl_hook_blocked_reason=%u\n"
                      "egl_hook_callback_count=%llu egl_hook_original_call_count=%llu "
                      "egl_hook_original_return_count=%llu\n"
                      "egl_snapshot_status=0x%08X status_namespace=mod_diagnostic\n",
                      egl.installAttempted, egl.installResult, egl.guardStatus, egl.blockedReason,
                      static_cast<unsigned long long>(egl.callbackCount),
                      static_cast<unsigned long long>(egl.originalCalls),
                      static_cast<unsigned long long>(egl.originalReturns), egl.snapshotStatus);
        if (egl.bindingKnown) {
            report.append("egl_current_api=0x%X egl_context_valid=%u egl_render_thread_id=%llu\n",
                          egl.api, egl.context != 0, static_cast<unsigned long long>(egl.thread));
        } else {
            report.append(
                "egl_current_api=unknown egl_context_valid=unknown egl_render_thread_id=unknown\n");
        }
        report.append(
            "egl_swap_count=%llu egl_context_change_count=%llu egl_surface_change_count=%llu\n"
            "gl_query_flags=%u query_semantics=issued_not_error_verified\n",
            static_cast<unsigned long long>(egl.swaps),
            static_cast<unsigned long long>(egl.contextChanges),
            static_cast<unsigned long long>(egl.surfaceChanges), egl.queryFlags);
        if (egl.queryFlags & diagnostics::kStringsIssued) {
            report.append("gl_version=%s\ngl_vendor=%s\ngl_renderer=%s\n", egl.version, egl.vendor,
                          egl.renderer);
        } else {
            report.append("gl_version=unknown gl_vendor=unknown gl_renderer=unknown\n");
        }
        if (egl.queryFlags & diagnostics::kGlslIssued) {
            report.append("glsl_version=%s\n", egl.glsl);
        } else {
            report.append("glsl_version=unknown\n");
        }
        if (egl.queryFlags & diagnostics::kViewportIssued) {
            report.append("gl_viewport=%d,%d,%d,%d\n", egl.viewport[0], egl.viewport[1],
                          egl.viewport[2], egl.viewport[3]);
        } else {
            report.append("gl_viewport=unknown\n");
        }
        if (egl.queryFlags & diagnostics::kFramebufferIssued) {
            report.append("gl_framebuffer_binding=%d\n", egl.framebuffer);
        } else {
            report.append("gl_framebuffer_binding=unknown\n");
        }
        if (egl.queryFlags & diagnostics::kProgramIssued) {
            report.append("gl_current_program=%d\n", egl.program);
        } else {
            report.append("gl_current_program=unknown\n");
        }
        if (egl.queryFlags & diagnostics::kVersionIssued) {
            report.append("gl_major=%d gl_minor=%d\n", egl.major, egl.minor);
        } else {
            report.append("gl_major=unknown gl_minor=unknown\n");
        }
        if (egl.queryFlags & diagnostics::kProfileIssued) {
            report.append("gl_context_profile_mask=%d\n", egl.profile);
        } else {
            report.append("gl_context_profile_mask=unknown\n");
        }
    } else {
        report.append("egl_observation=unknown_unreadable_or_unpublished\n");
    }
#endif
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
#if YW1_COMPANION_EGL_CALLBACK
    if (!observation.legacyModule && !reader.quarantined()) {
        observation.callbackReadResult = kExportChanged;
        for (unsigned attempt = 0; attempt < 2; ++attempt) {
            std::array<std::uint64_t, 1 + diagnostics::kCallbackWords> first{}, second{};
            auto result = reader.read(kCallbackOffset, first.data(), sizeof(first));
            if (result == 0) {
                result = reader.read(kCallbackOffset, second.data(), sizeof(second));
            }
            if (result != 0) { observation.callbackReadResult = result; break; }
            if (first[0] == 0 || (first[0] & 1) != 0 || first != second) { continue; }
            diagnostics::CallbackWire wire;
            std::memcpy(wire.words.data(), first.data() + 1, sizeof(wire));
            observation.callbackReadResult = diagnostics::validCallbackWire(wire) ? 0 : kInvalidData;
            if (observation.callbackReadResult == 0) { observation.callback = wire; }
            break;
        }
    }
#endif
#if YW1_EGL_CONTEXT_PROBE
    if (!observation.legacyModule && !observation.b028Profile && !reader.quarantined()) {
        observation.contextReadResult = kExportChanged;
        for (unsigned attempt = 0; attempt < 2; ++attempt) {
            std::array<std::uint64_t, 1 + diagnostics::kContextWords> first{}, second{};
            auto result = reader.read(kContextOffset, first.data(), sizeof(first));
            if (result == 0) { result = reader.read(kContextOffset, second.data(), sizeof(second)); }
            if (result != 0) { observation.contextReadResult = result; break; }
            if (first[0] == 0 || (first[0] & 1) || first != second) { continue; }
            diagnostics::ContextWire wire;
            static_assert(std::is_trivially_copyable_v<diagnostics::ContextWire>);
            std::memcpy(static_cast<void*>(&wire), first.data() + 1, sizeof(wire));
            observation.contextReadResult = diagnostics::validContextWire(wire) &&
                wire[diagnostics::ContextField::Profile] == YW1_CALLBACK_PROFILE ? 0 : kInvalidData;
            if (observation.contextReadResult == 0) { observation.context = wire; }
            break;
        }
        observation.cleanupFailed = reader.quarantined();
    }
#endif
#if YW1_COMPANION_EGL_GOT_PROBE
    inspectGot(reader, process, {modules, static_cast<std::size_t>(count)},
               static_cast<std::size_t>(count) < capacity, observation.pid, title, observation.got);
    observation.cleanupFailed = reader.quarantined();
#endif
#if YW1_IMGUI_DEMO_DIAGNOSTICS
    if (!observation.legacyModule && !observation.b028Profile && !reader.quarantined()) {
        diagnostics::DemoWire first{}, second{};
        auto rc = reader.read(kDemoOffset, &first, sizeof(first));
        if (rc == 0) { rc = reader.read(kDemoOffset, &second, sizeof(second)); }
        if (rc == 0 && (first != second || !diagnostics::validDemoWire(first, YW1_DEMO_PROFILE))) {
            rc = kInvalidData;
        }
        observation.demoReadResult = rc;
        if (rc == 0) { observation.demo = first; }
        observation.cleanupFailed = reader.quarantined();
    }
#endif
#if YW1_RENDER_DIAGNOSTICS
    if (!observation.legacyModule && !observation.b028Profile && !reader.quarantined()) {
        observation.renderReadResult = kExportChanged;
        for (unsigned attempt = 0; attempt < 2; ++attempt) {
            std::array<std::uint64_t, diagnostics::kRenderWords + 1> first{}, second{};
            auto rc = reader.read(kRenderOffset, first.data(), sizeof(first));
            if (rc == 0) { rc = reader.read(kRenderOffset, second.data(), sizeof(second)); }
            if (rc != 0) { observation.renderReadResult = rc; break; }
            if (!first[0] || (first[0] & 1) || first != second) { continue; }
            diagnostics::RenderWire wire;
            std::memcpy(wire.words.data(), first.data() + 1, sizeof(wire));
            observation.renderReadResult = diagnostics::validRenderWire(
                wire, YW1_RENDER_PROFILE, YW1_RENDER_MODE) ? 0 : kInvalidData;
            if (observation.renderReadResult == 0) { observation.render = wire; }
            break;
        }
        observation.cleanupFailed = reader.quarantined();
    }
#endif
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
