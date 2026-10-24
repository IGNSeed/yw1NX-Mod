#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace yw1::diagnostics {

using EglHandle = void *;
using GenericProc = void (*)();
// EGL公式ABIの32ビット戻り値を保持する。B.0.1にはHookを接続しない。
using EglBoolean = unsigned int;
using EglSwapBuffers = EglBoolean (*)(EglHandle, EglHandle);
static_assert(sizeof(EglBoolean) == 4);

struct EglQueryApi {
    EglHandle (*getCurrentContext)();
    EglHandle (*getCurrentDisplay)();
    EglHandle (*getCurrentSurface)(std::int32_t);
    GenericProc (*getProcAddress)(const char *);
    bool (*isExecutable)(std::uintptr_t);
    std::uint32_t (*queryApi)() = nullptr;
};

enum class SnapshotStatus : std::uint32_t {
    MissingEglFunction,
    NoContext,
    MissingGlFunction,
    InvalidVersion,
    UnsupportedApi,
    QueriesIssued,
    BindingChanged,
    InconsistentVersion,
    NotAttempted = 0xFFFFFFFFu
};

struct EglSnapshot {
    SnapshotStatus status = SnapshotStatus::MissingEglFunction;
    EglHandle context = nullptr;
    EglHandle display = nullptr;
    EglHandle surface = nullptr;
    std::uint64_t threadId = 0;
    std::uint64_t swapCount = 0;
    std::array<char, 256> version{};
    std::array<char, 128> vendor{};
    std::array<char, 128> renderer{};
    std::array<char, 256> shadingLanguageVersion{};
    std::array<std::int32_t, 4> viewport{};
    std::int32_t major = 0;
    std::int32_t minor = 0;
    std::int32_t profileMask = 0;
    std::int32_t framebuffer = 0;
    std::int32_t currentProgram = 0;
    bool versionIntegersQueried = false;
    bool profileQueried = false;
    bool framebufferQueried = false;
    bool programQueried = false;
    std::uint32_t currentApi = 0;
    bool apiQueried = false;
    bool versionTruncated = false;
};

// B.0.2用の未接続コレクター。呼出元が対象DB/ABI・同一current contextとthreadを保証する。
// isExecutableは照合済みモジュールの実行範囲を検証する契約で、単なる非null判定ではない。
// void型のGL queryは発行したことだけを記録し、エラーなしの成功を推測しない。
EglSnapshot captureEglSnapshot(const EglQueryApi &api, std::uint64_t threadId,
                               std::uint64_t swapCount);
bool sameEglBinding(const EglSnapshot &left, const EglSnapshot &right);

} // 名前空間 yw1::diagnostics
