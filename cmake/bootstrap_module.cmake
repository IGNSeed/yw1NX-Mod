# 固定版の依存をビルド領域へ取得し、公開リポジトリには取り込まない。
if(CMAKE_VERSION VERSION_LESS 3.24)
    message(FATAL_ERROR "Bootstrap dependency download requires CMake 3.24 or newer")
endif()
if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64" OR NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR "Bootstrap requires the devkitA64 Release toolchain")
endif()
enable_language(C ASM)

set(YW1_EXLAUNCH_REVISION "f9f4b0dd07b68f97958cb9c79228bbca22ca80d5")
set(YW1_EXLAUNCH_SOURCE_DIR "" CACHE PATH "Existing verified exlaunch source tree")
if(NOT YW1_EXLAUNCH_SOURCE_DIR)
    include(FetchContent)
    FetchContent_Declare(yw1_exlaunch
        URL "https://codeload.github.com/shadowninja108/exlaunch/tar.gz/${YW1_EXLAUNCH_REVISION}"
        URL_HASH SHA256=52dcf8c9acb9f8887a802836b9f409b8032d3f077711cbe1dce4cc7a72c5b97b
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
    FetchContent_MakeAvailable(yw1_exlaunch)
    set(YW1_EXLAUNCH_SOURCE_DIR "${yw1_exlaunch_SOURCE_DIR}")
endif()

# ローカル依存を指定した場合も、使用する全ソース・リンカー・ライセンスを照合する。
file(GLOB_RECURSE yw1_exlaunch_files RELATIVE "${YW1_EXLAUNCH_SOURCE_DIR}"
    "${YW1_EXLAUNCH_SOURCE_DIR}/source/*")
list(APPEND yw1_exlaunch_files "misc/link.ld" "LICENSE")
list(SORT yw1_exlaunch_files)
set(yw1_exlaunch_manifest "")
foreach(yw1_file IN LISTS yw1_exlaunch_files)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${YW1_EXLAUNCH_SOURCE_DIR}/${yw1_file}")
    file(SHA256 "${YW1_EXLAUNCH_SOURCE_DIR}/${yw1_file}" yw1_digest)
    string(APPEND yw1_exlaunch_manifest "${yw1_file}:${yw1_digest}\n")
endforeach()
string(SHA256 yw1_source_digest "${yw1_exlaunch_manifest}")
if(NOT yw1_source_digest STREQUAL "cc75a6a0fa57d5bf6d3c7e209339e7c3d46fcd9b7a5368ecef8264374f6d978f")
    message(FATAL_ERROR "The exlaunch source tree does not match the pinned revision")
endif()

set(yw1_exl "${YW1_EXLAUNCH_SOURCE_DIR}/source")
# Hook・入力・描画・任意シンボル再配置の実装はBootstrapにリンクしない。
set(yw1_bootstrap_upstream
    lib/init/crt0.s
    lib/init/init.cpp
    lib/module.cpp
    lib/log/svc_logger.cpp
    lib/diag/assert.cpp
    lib/util/random.cpp
    lib/util/version.cpp
    lib/util/sys/mem_layout.cpp
    lib/util/sys/rw_pages.cpp
    lib/nx/kernel/virtmem.c
    lib/nx/kernel/svc.s
    lib/nx/arm/cache.s
    lib/reloc/reloc.cpp
    rtld/relocation.cpp)
list(TRANSFORM yw1_bootstrap_upstream PREPEND "${yw1_exl}/")

add_executable(yw1_bootstrap
    ${yw1_bootstrap_upstream}
    "${PROJECT_SOURCE_DIR}/source/bootstrap/bootstrap.cpp"
    "${PROJECT_SOURCE_DIR}/source/bootstrap/module_memory.cpp"
    "${PROJECT_SOURCE_DIR}/source/bootstrap/process_handle.cpp")
set_target_properties(yw1_bootstrap PROPERTIES SUFFIX ".elf")
if(YW1_BOOTSTRAP_SD_LOGGING)
    target_sources(yw1_bootstrap PRIVATE
        "${PROJECT_SOURCE_DIR}/source/logging/memory_log.cpp"
        "${PROJECT_SOURCE_DIR}/source/logging/ipc_fs.cpp"
        "${PROJECT_SOURCE_DIR}/source/logging/sd_writer.cpp"
        "${PROJECT_SOURCE_DIR}/source/logging/sd_logger.cpp")
    target_compile_definitions(yw1_bootstrap PRIVATE YW1_BOOTSTRAP_SD_LOGGING=1)
    target_compile_options(yw1_bootstrap PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-fstack-usage>")
endif()
target_include_directories(yw1_bootstrap BEFORE PRIVATE
    "${PROJECT_SOURCE_DIR}/source" "${yw1_exl}" "${yw1_exl}/lib"
    "${DEVKITPRO}/libnx/include")
target_compile_features(yw1_bootstrap PRIVATE cxx_std_23)
target_compile_definitions(yw1_bootstrap PRIVATE
    __SWITCH__ __RTLD_6XX__ EXL_LOAD_KIND=Module EXL_LOAD_KIND_ENUM=2
    EXL_PROGRAM_ID=0x0100C0000CEEA000)
target_compile_options(yw1_bootstrap PRIVATE
    -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft
    -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections -Wall
    -Wno-format-zero-length
    "$<$<COMPILE_LANGUAGE:CXX>:-fno-rtti;-fno-exceptions;-fno-unwind-tables;-fno-asynchronous-unwind-tables>"
    "$<$<COMPILE_LANGUAGE:ASM>:-x;assembler-with-cpp>")

target_link_options(yw1_bootstrap PRIVATE
    -nostartfiles -shared
    "-Wl,-T,${YW1_EXLAUNCH_SOURCE_DIR}/misc/link.ld"
    -Wl,--export-dynamic,--gc-sections,--build-id=sha1,--hash-style=both
    -Wl,--no-undefined,--wrap=svcMapProcessMemory,--wrap=svcUnmapProcessMemory
    -Wl,-Bsymbolic,--exclude-libs=ALL
    "-Wl,--version-script=${PROJECT_SOURCE_DIR}/cmake/bootstrap_exports.ld"
    -Wl,-z,nodynamic-undefined-weak
    -Wl,-init=yw1_bootstrap_init
    "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/yw1_bootstrap.map")
set_property(TARGET yw1_bootstrap APPEND PROPERTY LINK_DEPENDS
    "${PROJECT_SOURCE_DIR}/cmake/bootstrap_exports.ld" "${YW1_EXLAUNCH_SOURCE_DIR}/misc/link.ld")

set(yw1_elf2nso "${DEVKITPRO}/tools/bin/elf2nso${YW1_TOOL_SUFFIX}")
if(NOT EXISTS "${yw1_elf2nso}")
    message(FATAL_ERROR "devkitPro elf2nso was not found")
endif()
add_custom_command(TARGET yw1_bootstrap POST_BUILD
    COMMAND "${yw1_elf2nso}" "$<TARGET_FILE:yw1_bootstrap>"
        "${CMAKE_CURRENT_BINARY_DIR}/yw1_bootstrap.nso"
    VERBATIM)
