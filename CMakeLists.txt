cmake_minimum_required(VERSION 3.21)
project(yw1nx_foundation VERSION 0.1.0 LANGUAGES CXX)

add_library(yw1nx_foundation STATIC
    source/game/versions/version_manager.cpp
    source/game/symbols/symbol_registry.cpp
)
target_include_directories(yw1nx_foundation PUBLIC source)
target_compile_features(yw1nx_foundation PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(yw1nx_foundation PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(yw1nx_foundation PRIVATE
        -Wall -Wextra -Wpedantic -Werror -fno-exceptions -fno-rtti)
endif()

option(YW1_BUILD_BOOTSTRAP "Build the Stage B.0.1 bootstrap module" OFF)
option(YW1_BOOTSTRAP_SD_LOGGING "Enable the bounded independent SD logging worker" OFF)
option(YW1_BOOTSTRAP_FS_DIAGNOSTIC "Isolate FS diagnostics from exlaunch initialization" OFF)
option(YW1_BOOTSTRAP_EXTERNAL_DIAGNOSTICS "Expose the bounded B.0.1.5 diagnostic ABI" OFF)
option(YW1_BUILD_DIAGNOSTICS_COMPANION "Build the read-only diagnostics NRO" OFF)
option(YW1_BOOTSTRAP_EXLAUNCH_DIAGNOSTIC "Verify exlaunch initialization without a direct FS writer" OFF)
option(YW1_BOOTSTRAP_EGL_DIAGNOSTIC "Inspect EGL hook prerequisites without installing a hook" OFF)
option(YW1_BOOTSTRAP_SAFE_HOOK_DIAGNOSTIC "Report blocked safe-hook prerequisites without installing" OFF)
option(YW1_COMPANION_EGL_GOT_PROBE "Build a read-only GOT companion for the unchanged B021 module" OFF)
option(YW1_EGL_HOOK_ENABLED "Enable the EGL hook after installer safety verification" OFF)
if(YW1_COMPANION_EGL_GOT_PROBE AND
    (YW1_BUILD_BOOTSTRAP OR NOT YW1_BUILD_DIAGNOSTICS_COMPANION OR
     NOT YW1_BOOTSTRAP_EXTERNAL_DIAGNOSTICS OR NOT YW1_BOOTSTRAP_EGL_DIAGNOSTIC OR
     NOT YW1_BOOTSTRAP_SAFE_HOOK_DIAGNOSTIC))
    message(FATAL_ERROR "GOT probe requires companion-only mode and the B021 external/EGL/safe ABI")
endif()
if(YW1_BOOTSTRAP_SAFE_HOOK_DIAGNOSTIC AND NOT YW1_BOOTSTRAP_EGL_DIAGNOSTIC)
    message(FATAL_ERROR "Safe hook diagnostics require the EGL diagnostic profile")
endif()
if(YW1_EGL_HOOK_ENABLED)
    message(FATAL_ERROR "EGL hook installer safety is unverified; hook-enabled artifacts are blocked")
endif()
if(YW1_BOOTSTRAP_EGL_DIAGNOSTIC AND
    (YW1_BOOTSTRAP_EXLAUNCH_DIAGNOSTIC OR
     (NOT YW1_BUILD_BOOTSTRAP AND NOT YW1_COMPANION_EGL_GOT_PROBE) OR
     NOT YW1_BOOTSTRAP_EXTERNAL_DIAGNOSTICS OR YW1_BOOTSTRAP_SD_LOGGING OR YW1_BOOTSTRAP_FS_DIAGNOSTIC))
    message(FATAL_ERROR "EGL diagnostics require bootstrap/external diagnostics and disable other profiles")
endif()
if(YW1_BOOTSTRAP_EXLAUNCH_DIAGNOSTIC AND
    (NOT YW1_BUILD_BOOTSTRAP OR NOT YW1_BOOTSTRAP_EXTERNAL_DIAGNOSTICS OR
     YW1_BOOTSTRAP_SD_LOGGING OR YW1_BOOTSTRAP_FS_DIAGNOSTIC))
    message(FATAL_ERROR "Exlaunch diagnostics require bootstrap/external diagnostics and disable FS profiles")
endif()
if(YW1_BOOTSTRAP_EXTERNAL_DIAGNOSTICS AND NOT YW1_BOOTSTRAP_FS_DIAGNOSTIC AND
    NOT YW1_BOOTSTRAP_EXLAUNCH_DIAGNOSTIC AND NOT YW1_BOOTSTRAP_EGL_DIAGNOSTIC)
    message(FATAL_ERROR "External diagnostics require the isolated FS diagnostic module")
endif()
if(YW1_BUILD_DIAGNOSTICS_COMPANION AND NOT YW1_BOOTSTRAP_EXTERNAL_DIAGNOSTICS)
    message(FATAL_ERROR "The companion requires the matching diagnostic module")
endif()
if(YW1_BOOTSTRAP_FS_DIAGNOSTIC AND NOT YW1_BOOTSTRAP_SD_LOGGING)
    message(FATAL_ERROR "FS diagnostics require YW1_BOOTSTRAP_SD_LOGGING")
endif()
if(YW1_BOOTSTRAP_SD_LOGGING AND NOT YW1_BUILD_BOOTSTRAP)
    message(FATAL_ERROR "SD logging requires YW1_BUILD_BOOTSTRAP")
endif()
if(YW1_BUILD_BOOTSTRAP)
    include(cmake/bootstrap_module.cmake)
elseif(YW1_COMPANION_EGL_GOT_PROBE)
    include(cmake/diagnostic_baseline.cmake)
endif()
if(YW1_BUILD_DIAGNOSTICS_COMPANION)
    include(cmake/diagnostics_companion.cmake)
endif()
