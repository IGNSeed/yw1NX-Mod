#include "diagnostic_target.hpp"
#include "game/symbols/symbol_registry.hpp"
#include "report.hpp"

namespace yw1::companion {
namespace {
constexpr auto kMainId = std::to_array(diagnostics::kMainBuildId);
constexpr game::BuildId kSdkId{0x86, 0xA2, 0xAF, 0xD8, 0x63, 0xC9, 0x90, 0x62, 0xD6, 0x66,
                               0x22, 0x6A, 0x9E, 0x7A, 0xA7, 0x64, 0xB6, 0x53, 0xEC, 0xEC};
constexpr game::BuildId kRtldId{0xB3, 0xA9, 0xEC, 0x8C, 0x84, 0x81, 0x06, 0xCA, 0x72, 0x4F,
                                0x78, 0x93, 0x82, 0x5E, 0x38, 0x7D, 0x91, 0x22, 0xC5, 0x91};
constexpr game::BuildId kSubsdk1Id{0xA4, 0xB6, 0x20, 0xD9, 0xC1, 0x40, 0xA4, 0xAC, 0x6E, 0x01,
                                   0x02, 0x51, 0xAF, 0xD5, 0xCB, 0x11, 0x0F, 0xEE, 0xFC, 0x14};

std::optional<std::uint64_t> resolveMainSymbol(const ModuleRecord& module, std::string_view name) {
    game::BuildId observed{};
    for (unsigned i = 0; i < 32; ++i) {
        observed[i] = module.buildId[i];
    }
    game::ModuleImage image{module.base, module.size, {diagnostics::kTitleId, "main", observed}};
    constexpr game::VersionProfile profiles[]{{diagnostics::kTitleId, "1.4.0", "main", kMainId}};
    const auto version = game::VersionManager(profiles).match(image.identity);
    constexpr game::SymbolRecord symbols[]{{"eglSwapBuffers.import_slot", "main", kMainId,
                                            kGotSlotOffset, 8, 8, game::EvidenceStatus::Verified},
                                           {"eglSwapBuffers.plt_resolver_entry", "main", kMainId,
                                            kInitialPltOffset, 32, 16,
                                            game::EvidenceStatus::Verified}};
    return version ? game::SymbolRegistry(symbols).resolve(name, *version, image) : std::nullopt;
}

bool validModule(const ModuleRecord& module) {
    return module.base != 0 && module.base % 4096 == 0 && module.size != 0 &&
           module.size % 4096 == 0 &&
           diagnostics::contains(module.base, module.size, module.base, module.size);
}
GotTarget identify(const ModuleRecord& module) {
    if (equalId(module.buildId, diagnostics::kMainBuildId)) {
        return GotTarget::Main;
    }
    if (equalId(module.buildId, diagnostics::kSubsdk0Id.data())) {
        return GotTarget::Subsdk0;
    }
    if (equalId(module.buildId, kSdkId.data())) {
        return GotTarget::Sdk;
    }
    if (equalId(module.buildId, kRtldId.data())) {
        return GotTarget::Rtld;
    }
    if (equalId(module.buildId, kSubsdk1Id.data())) {
        return GotTarget::Subsdk1;
    }
    if (equalId(module.buildId, kExpectedModuleId)) {
        return GotTarget::Bootstrap;
    }
    return GotTarget::OtherLoaded;
}
} // 無名名前空間

std::uint32_t validateGotRead(std::uint64_t pid, std::uint64_t title,
                              std::span<const ModuleRecord> modules, bool complete,
                              GotObservation& output, std::uint64_t offset) {
    if (pid == 0 || title != diagnostics::kTitleId) {
        return kWrongTarget;
    }
    if (!complete || modules.empty() || modules.size() > kLoaderQueryCapacity) {
        return kInvalidData;
    }
    const auto mainIndex = findModule(modules.data(), modules.size(), diagnostics::kMainBuildId);
    if (mainIndex < 0) {
        return mainIndex == -1 ? kWrongTarget : kInvalidData;
    }
    const auto& main = modules[mainIndex];
    output.mainVerified = true;
    output.mainBase = main.base;
    if (offset != kGotSlotOffset || !validModule(main) || !validRead(main, offset, 8)) {
        return kInvalidData;
    }
    const auto address = resolveMainSymbol(main, "eglSwapBuffers.import_slot");
    if (!address ||
        !diagnostics::contains(main.base, main.size, *address & ~std::uint64_t{4095}, 4096)) {
        return kInvalidData;
    }
    output.slotAddress = *address;
    return 0;
}

void classifyGot(std::span<const ModuleRecord> modules, GotObservation& output) {
    if (!output.valueKnown) {
        return;
    }
    if (output.slotValue == 0) {
        output.target = GotTarget::Null;
        output.matchesExpected = 0;
        return;
    }
    if (modules.empty() || modules.size() > kLoaderQueryCapacity) {
        output.target = GotTarget::Ambiguous;
        return;
    }
    int found = -1;
    for (std::size_t i = 0; i < modules.size(); ++i) {
        if (!validModule(modules[i])) {
            output.target = GotTarget::Ambiguous;
            return;
        }
        if (diagnostics::contains(modules[i].base, modules[i].size, output.slotValue, 1)) {
            if (found >= 0) {
                output.target = GotTarget::Ambiguous;
                return;
            }
            found = static_cast<int>(i);
        }
    }
    const auto sub = findModule(modules.data(), modules.size(), diagnostics::kSubsdk0Id.data());
    if (sub >= 0) {
        game::ModuleImage image{modules[sub].base,
                                modules[sub].size,
                                {diagnostics::kTitleId, "subsdk0", diagnostics::kSubsdk0Id}};
        const auto expected = diagnostics::resolveSwap(image);
        if (expected) {
            output.matchesExpected = output.slotValue == *expected ? 1 : 0;
        }
    }
    if (found < 0) {
        output.target = GotTarget::Outside;
        return;
    }
    const auto& module = modules[found];
    if (findModule(modules.data(), modules.size(), module.buildId) != found) {
        output.target = GotTarget::Ambiguous;
        return;
    }
    output.targetIndex = found;
    output.target = identify(module);
    output.targetOffset = output.slotValue - module.base;
    for (unsigned i = 0; i < 32; ++i) {
        output.targetId[i] = module.buildId[i];
    }
    output.normalizedKnown = true;
    const auto resolver = output.target == GotTarget::Main
                              ? resolveMainSymbol(module, "eglSwapBuffers.plt_resolver_entry")
                              : std::nullopt;
    if (resolver && output.slotValue == *resolver) {
        output.binding = GotBinding::LazyResolverCandidate;
    } else if (output.matchesExpected == 1) {
        output.binding = GotBinding::ExpectedExportAddress;
    }
}

bool sameNormalizedGot(const GotObservation& left, const GotObservation& right) {
    return left.normalizedKnown && right.normalizedKnown && left.targetId == right.targetId &&
           left.targetOffset == right.targetOffset;
}
const char* gotTargetName(GotTarget target) {
    switch (target) {
    case GotTarget::Null:
        return "null";
    case GotTarget::Main:
        return "main";
    case GotTarget::Subsdk0:
        return "subsdk0";
    case GotTarget::Sdk:
        return "sdk";
    case GotTarget::Rtld:
        return "rtld";
    case GotTarget::Subsdk1:
        return "subsdk1";
    case GotTarget::Bootstrap:
        return "bootstrap";
    case GotTarget::OtherLoaded:
        return "other_loader_module";
    case GotTarget::Outside:
        return "outside_loader_modules";
    case GotTarget::Ambiguous:
        return "ambiguous_or_invalid_modules";
    default:
        return "unknown";
    }
}
const char* gotBindingName(GotBinding binding) {
    switch (binding) {
    case GotBinding::LazyResolverCandidate:
        return "lazy_resolver_candidate";
    case GotBinding::ExpectedExportAddress:
        return "expected_export_address";
    case GotBinding::ExecutableCandidate:
        return "executable_nonexport_candidate_not_verified_thunk";
    case GotBinding::NonExecutable:
        return "non_executable";
    default:
        return "unknown";
    }
}
void appendGotReport(Report& report, const Observation& observation,
                     const BootstrapLog& bootstrap) {
    const auto& got = observation.got;
    const auto boolean = [](int value) {
        return value < 0 ? "unknown" : (value ? "true" : "false");
    };
    report.append("\n=== YW1 EGL GOT Probe B0.2.2 ===\nsource=companion-nro\nmode=read-only\n"
                  "observation_source=companion-nro observation_stage=album_snapshot\n"
                  "album_snapshot_is_not_continuous_gameplay_observation=true\n"
                  "snapshot_consistency=separate_loader_slot_and_page_observations\n"
                  "application_pid=%llu got_probe_attempted=%u main_build_id_verified=%s\n",
                  static_cast<unsigned long long>(observation.pid), got.attempted,
                  boolean(got.mainVerified ? 1 : 0));
    report.result("got_probe_result",
                  got.result == kNotAttempted ? observation.result : got.result);
    report.result("got_slot_read_result", got.readResult);
    report.result("game_got_page_query_result", got.pageQueryResult);
    report.result("got_target_page_query_result", got.targetQueryResult);
    report.append("got_slot_offset=0x8B4398\n");
    if (got.mainVerified) {
        report.append("main_module_base=0x%016llX target_build_id=",
                      static_cast<unsigned long long>(got.mainBase));
        for (auto byte : diagnostics::kMainBuildId) {
            report.append("%02X", byte);
        }
        report.append("\n");
    } else {
        report.append("main_module_base=unknown target_build_id=unknown\n");
    }
    if (got.valueKnown) {
        report.append("got_slot_value=0x%016llX companion_got_value=0x%016llX\n",
                      static_cast<unsigned long long>(got.slotValue),
                      static_cast<unsigned long long>(got.slotValue));
    } else {
        report.append("got_slot_value=unknown companion_got_value=unknown\n");
    }
    report.append(
        "got_target_module=%s target_matches_expected_export=%s target_executable=%s\n"
        "target_executable_semantics=game_page_permission_only function_or_thunk_verified=false\n"
        "binding_state=%s observation_valid=%s normalized_target_valid=%s\n",
        gotTargetName(got.target), boolean(got.matchesExpected), boolean(got.executable),
        gotBindingName(got.binding), boolean(got.valueKnown && got.result == 0),
        boolean(got.normalizedKnown));
    if (got.normalizedKnown) {
        report.append("got_target_offset=0x%llX got_target_module_index=%d got_target_build_id=",
                      static_cast<unsigned long long>(got.targetOffset), got.targetIndex);
        for (auto byte : got.targetId) {
            report.append("%02X", byte);
        }
        report.append("\nnormalized_target=%s+0x%llX comparison_key=full_build_id_and_offset\n",
                      gotTargetName(got.target), static_cast<unsigned long long>(got.targetOffset));
    } else {
        report.append(
            "got_target_offset=unknown got_target_build_id=unknown normalized_target=unknown\n");
    }
    if (got.pagePermission != UINT32_MAX) {
        report.append("game_got_page_permission=0x%X page_permission_scope=game_process_handle\n",
                      got.pagePermission);
    } else {
        report.append("game_got_page_permission=unknown\n");
    }
    if (got.targetPermission != UINT32_MAX) {
        report.append("target_page_permission=0x%X\n", got.targetPermission);
    } else {
        report.append("target_page_permission=unknown\n");
    }
    if (bootstrap.readResult == 0) {
        report.append("observation_boot_token=0x%016llX bootstrap_module_base=0x%016llX\n",
                      static_cast<unsigned long long>(bootstrap.header.bootToken),
                      static_cast<unsigned long long>(bootstrap.moduleBase));
    } else {
        report.append("observation_boot_token=unknown\n");
    }
    report.append("bootstrap_got_value=not_collected_module_unchanged\n"
                  "got_hook_install_attempted=0 got_modification=disabled\n");
}
} // 名前空間 yw1::companion
