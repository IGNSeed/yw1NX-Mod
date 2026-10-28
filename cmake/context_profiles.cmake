# B028実機確認版のA/Bを完全IDとファイルSHAで固定し、旧Callback ABIを読む。
set(YW1_CONTEXT_CALLBACK_BASELINE_DIR "" CACHE PATH "Verified B028 deploy directory")
set(context_baseline_prefixes B028A B028B)
set(context_baseline_directories symbols profiles/canary/symbols)
set(context_baseline_elf_hashes
    8d364f7f1d0fc297741103cad296c2ad5ec2ffff259f613211e32b6c8a316bf1
    3bb53e5dcd75b304388cc634ae28e8b06d3d55092940fef0bd475c67fd3060a6)
set(context_baseline_nso_hashes
    98229f4d74f66703dd6fefdfbedc892c2e4f4b094b955e85c202f1941694a01a
    d8734c632f8c302c6a11a9a5f5329eccdbae54ba1398644fc78de6135f6b15ab)
foreach(index RANGE 0 1)
    list(GET context_baseline_prefixes ${index} prefix)
    list(GET context_baseline_directories ${index} relative)
    set(elf "${YW1_CONTEXT_CALLBACK_BASELINE_DIR}/${relative}/yw1_bootstrap.elf")
    if(index EQUAL 0)
        set(nso "${YW1_CONTEXT_CALLBACK_BASELINE_DIR}/atmosphere/contents/0100C0000CEEA000/exefs/subsdk9")
    else()
        set(nso "${YW1_CONTEXT_CALLBACK_BASELINE_DIR}/profiles/canary/atmosphere/contents/0100C0000CEEA000/exefs/subsdk9")
    endif()
    foreach(kind elf nso)
        if(NOT EXISTS "${${kind}}" OR IS_SYMLINK "${${kind}}")
            message(FATAL_ERROR "Missing verified B028 baseline")
        endif()
        file(SHA256 "${${kind}}" actual)
        list(GET context_baseline_${kind}_hashes ${index} expected)
        if(NOT actual STREQUAL expected)
            message(FATAL_ERROR "B028 baseline hash mismatch")
        endif()
    endforeach()
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DNM=${CMAKE_NM}" "-DELF=${elf}"
        "-DNSO=${nso}" -DEGL_DIAGNOSTIC=ON -DCALLBACK_DIAGNOSTIC=ON "-DPREFIX=${prefix}"
        "-DOUTPUT=${CMAKE_CURRENT_BINARY_DIR}/generated/${prefix}_diagnostic_target.hpp"
        -P "${PROJECT_SOURCE_DIR}/cmake/diagnostic_target.cmake" RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Cannot generate verified B028 target")
    endif()
endforeach()
