# 実機PASSした自作B021の3成果物だけを再利用し、Moduleを再ビルドしない。
if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64" OR NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR "The GOT companion requires devkitA64 Release")
endif()
set(YW1_DIAGNOSTICS_BASELINE_DIR "" CACHE PATH "Verified B021 build artifact directory")
set(baseline_files yw1_bootstrap.elf yw1_bootstrap.nso yw1_bootstrap.map)
set(baseline_hashes
    d5b813ee6205f4c04578dbca7280a6044a7a73b2b8e226ab7f305b2f659e7dcb
    5de7d68fd89380ce08da795c89cb06994cd2969cf1ff2858a6b0d8f7bfd60d8b
    4e8d61dd3fb79456b2e0e6973dbb74d4409b118170c28b4a12395e86ee2af7ea)
foreach(index RANGE 0 2)
    list(GET baseline_files ${index} name)
    list(GET baseline_hashes ${index} expected)
    set(input "${YW1_DIAGNOSTICS_BASELINE_DIR}/${name}")
    if(NOT EXISTS "${input}" OR IS_SYMLINK "${input}")
        message(FATAL_ERROR "Missing verified B021 baseline: ${name}")
    endif()
    file(SHA256 "${input}" actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "B021 baseline hash mismatch: ${name}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${input}")
    configure_file("${input}" "${CMAKE_CURRENT_BINARY_DIR}/${name}" COPYONLY)
endforeach()
execute_process(COMMAND "${CMAKE_COMMAND}" "-DNM=${CMAKE_NM}"
    "-DELF=${CMAKE_CURRENT_BINARY_DIR}/yw1_bootstrap.elf"
    "-DNSO=${CMAKE_CURRENT_BINARY_DIR}/yw1_bootstrap.nso" "-DEGL_DIAGNOSTIC=ON"
    "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generated/diagnostic_target.hpp"
    -P "${PROJECT_SOURCE_DIR}/cmake/diagnostic_target.cmake" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Cannot generate the verified B021 module target")
endif()
add_custom_target(yw1_bootstrap)
