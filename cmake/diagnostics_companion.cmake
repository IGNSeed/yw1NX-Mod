# NROだけを通常のlibnx環境へリンクし、ゲーム用Moduleとは起動・TLSを分離する。
add_executable(yw1_diagnostics
    source/companion/main.cpp
    source/companion/reader.cpp
    source/companion/file_log.cpp)
set_target_properties(yw1_diagnostics PROPERTIES SUFFIX ".elf")
add_dependencies(yw1_diagnostics yw1_bootstrap)
target_include_directories(yw1_diagnostics PRIVATE source "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_include_directories(yw1_diagnostics SYSTEM PRIVATE "${DEVKITPRO}/libnx/include")
target_compile_features(yw1_diagnostics PRIVATE cxx_std_20)
target_compile_definitions(yw1_diagnostics PRIVATE __SWITCH__)
set(yw1_diagnostic_version "B0.1.6")
if(YW1_BOOTSTRAP_EXLAUNCH_DIAGNOSTIC)
    target_compile_definitions(yw1_diagnostics PRIVATE YW1_BOOTSTRAP_EXLAUNCH_DIAGNOSTIC=1)
    set(yw1_diagnostic_version "B0.1.7")
endif()
if(YW1_BOOTSTRAP_EGL_DIAGNOSTIC)
    target_compile_definitions(yw1_diagnostics PRIVATE YW1_BOOTSTRAP_EGL_DIAGNOSTIC=1)
    set(yw1_diagnostic_version "B0.2")
endif()
if(YW1_BOOTSTRAP_SAFE_HOOK_DIAGNOSTIC)
    target_compile_definitions(yw1_diagnostics PRIVATE YW1_BOOTSTRAP_SAFE_HOOK_DIAGNOSTIC=1)
    set(yw1_diagnostic_version "B0.2.1")
endif()
if(YW1_COMPANION_EGL_GOT_PROBE)
    target_sources(yw1_diagnostics PRIVATE source/companion/got_probe.cpp)
    target_link_libraries(yw1_diagnostics PRIVATE yw1nx_foundation)
    target_compile_definitions(yw1_diagnostics PRIVATE YW1_COMPANION_EGL_GOT_PROBE=1)
    set(yw1_diagnostic_version "B0.2.2")
endif()
if(YW1_COMPANION_EGL_TARGET_MEMORY)
    target_sources(yw1_diagnostics PRIVATE source/companion/target_memory.cpp)
    target_compile_definitions(yw1_diagnostics PRIVATE YW1_COMPANION_EGL_TARGET_MEMORY=1)
    set(yw1_diagnostic_version "B0.2.3")
endif()
if(YW1_COMPANION_EGL_CODE_SNAPSHOT)
    target_sources(yw1_diagnostics PRIVATE source/companion/code_snapshot.cpp)
    target_compile_definitions(yw1_diagnostics PRIVATE YW1_COMPANION_EGL_CODE_SNAPSHOT=1)
    set(yw1_diagnostic_version "B0.2.4")
endif()
if(YW1_COMPANION_EGL_CODE_ATTRIBUTION)
    target_compile_definitions(yw1_diagnostics PRIVATE YW1_COMPANION_EGL_CODE_ATTRIBUTION=1)
    set(yw1_diagnostic_version "B0.2.5")
endif()
target_compile_options(yw1_diagnostics PRIVATE -O2 -g -march=armv8-a+crc+crypto -mtune=cortex-a57
    -mtp=soft -fPIE -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti -Wall -Wextra
    -Wno-missing-field-initializers)
target_link_directories(yw1_diagnostics PRIVATE "${DEVKITPRO}/libnx/lib")
# SDK本体は変更せず、非PICのdevoptab定数表だけをNROのRW再配置領域へ配置する。
file(READ "${DEVKITPRO}/libnx/switch.ld" nro_script)
set(ro_selector "*(.rodata .rodata.* .gnu.linkonce.r.*)")
if(NOT nro_script MATCHES "\\*\\(\\.rodata \\.rodata\\.\\* \\.gnu\\.linkonce\\.r\\.\\*\\)")
    message(FATAL_ERROR "Unsupported libnx linker script")
endif()
string(REPLACE "${ro_selector}"
    "*(.rodata EXCLUDE_FILE (*iosupport.o) .rodata.* .gnu.linkonce.r.*)"
    nro_script "${nro_script}")
string(REPLACE ".data.rel.ro :\n\t{" ".data.rel.ro :\n\t{\n        *iosupport.o(.rodata .rodata.*)"
    nro_script "${nro_script}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/yw1_diagnostics.ld" "${nro_script}")
file(READ "${DEVKITPRO}/libnx/switch.specs" nro_specs)
string(REPLACE "%:getenv(DEVKITPRO /libnx/switch.ld)"
    "${CMAKE_CURRENT_BINARY_DIR}/yw1_diagnostics.ld"
    nro_specs "${nro_specs}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/yw1_diagnostics.specs" "${nro_specs}")
target_link_options(yw1_diagnostics PRIVATE
    "-specs=${CMAKE_CURRENT_BINARY_DIR}/yw1_diagnostics.specs"
    -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft
    -Wl,--wrap=framebufferCreate,--wrap=framebufferMakeLinear
    "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/yw1_diagnostics.map")
set_property(TARGET yw1_diagnostics APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_CURRENT_BINARY_DIR}/yw1_diagnostics.ld")
target_link_libraries(yw1_diagnostics PRIVATE nx)
set_property(TARGET yw1_diagnostics PROPERTY RULE_LAUNCH_LINK
    "\"${CMAKE_COMMAND}\" -E env DEVKITPRO=${DEVKITPRO}")
add_custom_command(TARGET yw1_diagnostics POST_BUILD
    COMMAND "${DEVKITPRO}/tools/bin/nacptool${YW1_TOOL_SUFFIX}" --create
        "YW1 Diagnostics" "YW1NX-Mod" "${yw1_diagnostic_version}" "${CMAKE_CURRENT_BINARY_DIR}/yw1_diagnostics.nacp"
    COMMAND "${DEVKITPRO}/tools/bin/elf2nro${YW1_TOOL_SUFFIX}" "$<TARGET_FILE:yw1_diagnostics>"
        "${CMAKE_CURRENT_BINARY_DIR}/yw1_diagnostics.nro"
        "--nacp=${CMAKE_CURRENT_BINARY_DIR}/yw1_diagnostics.nacp"
    VERBATIM)
