#include "egl_snapshot.hpp"

namespace yw1::diagnostics {
namespace {

constexpr std::int32_t kEglDraw = 0x3059;
constexpr std::uint32_t kGlVersion = 0x1F02;
constexpr std::uint32_t kGlVendor = 0x1F00;
constexpr std::uint32_t kGlRenderer = 0x1F01;
constexpr std::uint32_t kGlShadingLanguageVersion = 0x8B8C;
constexpr std::uint32_t kGlMajorVersion = 0x821B;
constexpr std::uint32_t kGlMinorVersion = 0x821C;
constexpr std::uint32_t kGlContextProfileMask = 0x9126;
constexpr std::uint32_t kGlViewport = 0x0BA2;
constexpr std::uint32_t kGlFramebufferBinding = 0x8CA6;
constexpr std::uint32_t kGlCurrentProgram = 0x8B8D;

using GlGetString = const unsigned char* (*)(std::uint32_t);
using GlGetInteger = void (*)(std::uint32_t, std::int32_t*);

template <typename Pointer> bool validPointer(const EglQueryApi& api, Pointer pointer) {
    return pointer != nullptr && api.isExecutable != nullptr &&
           api.isExecutable(reinterpret_cast<std::uintptr_t>(pointer));
}

template <std::size_t Size>
void copyString(std::array<char, Size>& output, const unsigned char* input) {
    if (input == nullptr) {
        return;
    }
    for (std::size_t index = 0; index + 1 < Size && input[index] != 0; ++index) {
        output[index] = static_cast<char>(input[index]);
    }
}

bool readNumber(const char*& cursor, std::int32_t& value) {
    unsigned int digits = 0;
    while (*cursor >= '0' && *cursor <= '9' && digits < 3) {
        value = value * 10 + (*cursor++ - '0');
        ++digits;
    }
    return digits != 0 && !(*cursor >= '0' && *cursor <= '9');
}

} // 無名名前空間

EglSnapshot captureEglSnapshot(const EglQueryApi& api, std::uint64_t threadId,
                               std::uint64_t swapCount) {
    EglSnapshot output;
    output.threadId = threadId;
    output.swapCount = swapCount;
    if (!validPointer(api, api.getCurrentContext)) {
        return output;
    }
    output.context = api.getCurrentContext();
    if (output.context == nullptr) {
        output.status = SnapshotStatus::NoContext;
        return output;
    }
    if (!validPointer(api, api.getCurrentDisplay) || !validPointer(api, api.getCurrentSurface) ||
        !validPointer(api, api.getProcAddress)) {
        return output;
    }
    output.display = api.getCurrentDisplay();
    output.surface = api.getCurrentSurface(kEglDraw);
    const auto getStringProc = api.getProcAddress("glGetString");
    const auto getIntegerProc = api.getProcAddress("glGetIntegerv");
    if (!validPointer(api, getStringProc) || !validPointer(api, getIntegerProc)) {
        output.status = SnapshotStatus::MissingGlFunction;
        return output;
    }
    const auto getString = reinterpret_cast<GlGetString>(getStringProc);
    const auto getInteger = reinterpret_cast<GlGetInteger>(getIntegerProc);
    copyString(output.version, getString(kGlVersion));
    copyString(output.vendor, getString(kGlVendor));
    copyString(output.renderer, getString(kGlRenderer));
    const char* cursor = output.version.data();
    if (cursor[0] == 'O') {
        // ES等の別APIへdesktop専用enumを送らない。
        output.status = SnapshotStatus::UnsupportedApi;
        return output;
    }
    if (!readNumber(cursor, output.major) || *cursor++ != '.' ||
        !readNumber(cursor, output.minor) || output.major == 0 ||
        (*cursor != '\0' && *cursor != ' ' && *cursor != '.')) {
        output.status = SnapshotStatus::InvalidVersion;
        return output;
    }
    getInteger(kGlViewport, output.viewport.data());
    if (output.major >= 2) {
        copyString(output.shadingLanguageVersion, getString(kGlShadingLanguageVersion));
        getInteger(kGlCurrentProgram, &output.currentProgram);
        output.programQueried = true;
    }
    if (output.major >= 3) {
        // FRAMEBUFFER_BINDINGとDRAW_FRAMEBUFFER_BINDINGはdesktop GLでは同じenum。
        getInteger(kGlFramebufferBinding, &output.framebuffer);
        output.framebufferQueried = true;
        getInteger(kGlMajorVersion, &output.major);
        getInteger(kGlMinorVersion, &output.minor);
        output.versionIntegersQueried = true;
        if (output.major > 3 || (output.major == 3 && output.minor >= 2)) {
            getInteger(kGlContextProfileMask, &output.profileMask);
            output.profileQueried = true;
        }
    }
    output.status = SnapshotStatus::QueriesIssued;
    return output;
}

bool sameEglBinding(const EglSnapshot& left, const EglSnapshot& right) {
    return left.context == right.context && left.display == right.display &&
           left.surface == right.surface && left.threadId == right.threadId;
}

} // 名前空間 yw1::diagnostics
