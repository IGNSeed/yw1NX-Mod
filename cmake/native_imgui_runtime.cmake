# 固定MIT formatterをbuild領域だけへ取得する。Private referenceを依存にしない。
set(yw1_stb_revision "2b2e64bc4778df6a06fbea3a5dbe61f7a76ecf19")
set(YW1_STB_SPRINTF_HEADER "" CACHE FILEPATH "Pinned stb_sprintf header")
if(NOT YW1_STB_SPRINTF_HEADER)
    set(YW1_STB_SPRINTF_HEADER "${CMAKE_CURRENT_BINARY_DIR}/generated/stb_sprintf.h")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
    file(DOWNLOAD "https://raw.githubusercontent.com/nothings/stb/${yw1_stb_revision}/stb_sprintf.h"
        "${YW1_STB_SPRINTF_HEADER}"
        EXPECTED_HASH SHA256=e0b8c56e1084602290b9a17ef59f429b5293deb8cb9d8b0f20a14abc39b56520
        TLS_VERIFY ON)
endif()
file(SHA256 "${YW1_STB_SPRINTF_HEADER}" yw1_stb_hash)
set_property(TARGET yw1_render_foundation PROPERTY YW1_STB_HEADER "${YW1_STB_SPRINTF_HEADER}")
if(NOT yw1_stb_hash STREQUAL "e0b8c56e1084602290b9a17ef59f429b5293deb8cb9d8b0f20a14abc39b56520")
    message(FATAL_ERROR "stb_sprintf header differs from the pinned MIT source")
endif()
target_compile_definitions(yw1_render_foundation PRIVATE
    IMGUI_DISABLE_DEFAULT_ALLOCATORS IMGUI_DISABLE_FILE_FUNCTIONS IMGUI_USE_STB_SPRINTF
    IMGUI_DISABLE_TIME_FUNCTIONS IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS
    STB_SPRINTF_STATIC STB_SPRINTF_NOUNALIGNED
    "IMGUI_STB_SPRINTF_FILENAME=\"${YW1_STB_SPRINTF_HEADER}\"")
# 全ImGui処理は既存renderer leaseが直列化する。guardの例外Runtimeを持ち込まない。
target_compile_options(yw1_render_foundation PRIVATE -fno-threadsafe-statics)
if(YW1_IMGUI_RENDER_MODE STREQUAL "MINIMAL")
    target_compile_definitions(yw1_render_foundation PRIVATE YW1_NATIVE_MINIMAL_ONLY=1)
endif()
# 公式Demoの固定31箇所だけを、型で容量が決まる自作formatterへ置換する。
file(READ "${YW1_IMGUI_SOURCE_DIR}/imgui_demo.cpp" yw1_demo_source)
string(REGEX MATCHALL "sprintf\\(" yw1_demo_calls "${yw1_demo_source}")
list(LENGTH yw1_demo_calls yw1_demo_call_count)
if(NOT yw1_demo_call_count EQUAL 31)
    message(FATAL_ERROR "Pinned official Demo formatter anchors differ")
endif()
string(REPLACE "sprintf(" "yw1::render::demoFormat(" yw1_demo_source "${yw1_demo_source}")
set(yw1_native_demo "${CMAKE_CURRENT_BINARY_DIR}/generated/imgui_demo_bounded.cpp")
file(WRITE "${yw1_native_demo}" "// 固定公式Demoの書式化だけを有界化。原本とMIT表記を保持。\n#include \"render/demo_format.hpp\"\n${yw1_demo_source}")
get_target_property(yw1_render_sources yw1_render_foundation SOURCES)
list(REMOVE_ITEM yw1_render_sources "${YW1_IMGUI_SOURCE_DIR}/imgui_demo.cpp")
set_property(TARGET yw1_render_foundation PROPERTY SOURCES "${yw1_render_sources}")
target_sources(yw1_render_foundation PRIVATE "${yw1_native_demo}")
