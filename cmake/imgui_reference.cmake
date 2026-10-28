# 上流のコンパイル検証専用。ゲームModuleやNROへはリンクしない。
if(NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64" OR NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(FATAL_ERROR "ImGui reference requires the devkitA64 Release toolchain")
endif()
set(YW1_IMGUI_REVISION "4be0b47ecc0edd535a40a79f6ceb6f1ac5936483")
set(YW1_IMGUI_SOURCE_DIR "" CACHE PATH "Verified independent official ImGui source tree")
if(NOT YW1_IMGUI_SOURCE_DIR)
    include(FetchContent)
    FetchContent_Declare(yw1_imgui
        URL "https://codeload.github.com/ocornut/imgui/zip/${YW1_IMGUI_REVISION}"
        URL_HASH SHA256=de47237a08716941b345f395c8ccbc689b85c6cc2c73d5138cfb0500340a2bc9
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
    FetchContent_MakeAvailable(yw1_imgui)
    set(YW1_IMGUI_SOURCE_DIR "${yw1_imgui_SOURCE_DIR}")
endif()
set(yw1_imgui_files LICENSE.txt imconfig.h imgui.cpp imgui.h imgui_demo.cpp imgui_draw.cpp
    imgui_internal.h imgui_tables.cpp imgui_widgets.cpp imstb_rectpack.h imstb_textedit.h
    imstb_truetype.h backends/imgui_impl_opengl3.cpp backends/imgui_impl_opengl3.h
    backends/imgui_impl_opengl3_loader.h)
list(SORT yw1_imgui_files)
set(yw1_imgui_manifest "")
foreach(yw1_file IN LISTS yw1_imgui_files)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${YW1_IMGUI_SOURCE_DIR}/${yw1_file}")
    file(SHA256 "${YW1_IMGUI_SOURCE_DIR}/${yw1_file}" yw1_digest)
    string(APPEND yw1_imgui_manifest "${yw1_file}:${yw1_digest}\n")
endforeach()
string(SHA256 yw1_imgui_digest "${yw1_imgui_manifest}")
if(NOT yw1_imgui_digest STREQUAL "15916057a66b9b3deeb38aba53124062166e04f029643484ee52c18471477665")
    message(FATAL_ERROR "ImGui reference sources do not match the pinned revision")
endif()
add_library(yw1_imgui_reference STATIC EXCLUDE_FROM_ALL
    "${YW1_IMGUI_SOURCE_DIR}/imgui.cpp" "${YW1_IMGUI_SOURCE_DIR}/imgui_draw.cpp"
    "${YW1_IMGUI_SOURCE_DIR}/imgui_tables.cpp" "${YW1_IMGUI_SOURCE_DIR}/imgui_widgets.cpp"
    "${YW1_IMGUI_SOURCE_DIR}/imgui_demo.cpp"
    "${YW1_IMGUI_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp"
    source/diagnostics/imgui_reference_probe.cpp)
target_include_directories(yw1_imgui_reference PRIVATE "${YW1_IMGUI_SOURCE_DIR}")
target_compile_features(yw1_imgui_reference PRIVATE cxx_std_20)
target_compile_definitions(yw1_imgui_reference PRIVATE IMGUI_IMPL_OPENGL_LOADER_CUSTOM)
# loaderヘッダーは型・extern宣言のみ。PC用gl3w実装はコンパイルしない。
set_source_files_properties("${YW1_IMGUI_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp"
    PROPERTIES COMPILE_OPTIONS "-include;${YW1_IMGUI_SOURCE_DIR}/backends/imgui_impl_opengl3_loader.h")
target_compile_options(yw1_imgui_reference PRIVATE -fPIC -fno-exceptions -fno-rtti)
