# 完成したNSOとELFから、このpairだけを読めるNROの定数を生成する。
execute_process(COMMAND "${NM}" -S --defined-only "${ELF}"
    RESULT_VARIABLE result OUTPUT_VARIABLE symbols)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Cannot inspect the diagnostic ELF")
endif()
foreach(name IN ITEMS yw1_module_identity yw1_module_diagnostics_state)
    string(REGEX MATCH "([0-9a-fA-F]+) ([0-9a-fA-F]+) [A-Za-z] ${name}(\n|$)" row "${symbols}")
    if(NOT row)
        message(FATAL_ERROR "Missing diagnostic symbol ${name}")
    endif()
    set(${name}_offset "0x${CMAKE_MATCH_1}")
    set(${name}_size "0x${CMAKE_MATCH_2}")
endforeach()
file(READ "${NSO}" build_id OFFSET 64 LIMIT 32 HEX)
string(LENGTH "${build_id}" length)
if(NOT length EQUAL 64)
    message(FATAL_ERROR "Invalid NSO Build ID")
endif()
set(bytes "")
foreach(index RANGE 0 31)
    math(EXPR start "${index} * 2")
    string(SUBSTRING "${build_id}" ${start} 2 byte)
    string(APPEND bytes "0x${byte},")
endforeach()
get_filename_component(directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${directory}")
file(WRITE "${OUTPUT}" "#pragma once\n#include <cstdint>\nnamespace yw1::companion {\n"
    "inline constexpr std::uint8_t kExpectedModuleId[32]{${bytes}};\n"
    "inline constexpr std::uint64_t kIdentityOffset=${yw1_module_identity_offset};\n"
    "inline constexpr std::uint64_t kStateOffset=${yw1_module_diagnostics_state_offset};\n"
    "inline constexpr std::uint64_t kStateSize=${yw1_module_diagnostics_state_size};\n}\n")
