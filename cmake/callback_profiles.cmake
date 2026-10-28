# 旧B021の完全IDと既知offsetだけを互換読取へ登録し、任意Moduleへは拡張しない。
set(YW1_DIAGNOSTICS_BASELINE_DIR "" CACHE PATH "Verified B021 baseline")
set(callback_legacy_files yw1_bootstrap.elf yw1_bootstrap.nso yw1_bootstrap.map)
set(callback_legacy_hashes
    d5b813ee6205f4c04578dbca7280a6044a7a73b2b8e226ab7f305b2f659e7dcb
    5de7d68fd89380ce08da795c89cb06994cd2969cf1ff2858a6b0d8f7bfd60d8b
    4e8d61dd3fb79456b2e0e6973dbb74d4409b118170c28b4a12395e86ee2af7ea)
foreach(index RANGE 0 2)
    list(GET callback_legacy_files ${index} name)
    list(GET callback_legacy_hashes ${index} expected)
    set(input "${YW1_DIAGNOSTICS_BASELINE_DIR}/${name}")
    if(NOT EXISTS "${input}" OR IS_SYMLINK "${input}")
        message(FATAL_ERROR "Missing verified legacy baseline")
    endif()
    file(SHA256 "${input}" actual)
    if(NOT actual STREQUAL expected)
        message(FATAL_ERROR "Legacy baseline hash mismatch")
    endif()
endforeach()
execute_process(COMMAND "${CMAKE_COMMAND}" "-DNM=${CMAKE_NM}"
    "-DELF=${YW1_DIAGNOSTICS_BASELINE_DIR}/yw1_bootstrap.elf"
    "-DNSO=${YW1_DIAGNOSTICS_BASELINE_DIR}/yw1_bootstrap.nso" "-DEGL_DIAGNOSTIC=ON"
    -DPREFIX=Legacy "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generated/legacy_diagnostic_target.hpp"
    -P "${PROJECT_SOURCE_DIR}/cmake/diagnostic_target.cmake" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Cannot generate verified legacy target")
endif()
