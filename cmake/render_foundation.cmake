# 実描画コードは独立archive。実機Moduleの許可条件とは分離してコンパイルする。
include(cmake/imgui_safe_patch.cmake)
set(YW1_IMGUI_SOURCE_DIR "" CACHE PATH "Verified independent official ImGui source tree")
if(NOT TARGET yw1_imgui_reference)
    include(cmake/imgui_reference.cmake)
endif()
set(yw1_render "${PROJECT_SOURCE_DIR}/source/render")
add_library(yw1_render_guards STATIC
    "${yw1_render}/imgui_arena.cpp" "${yw1_render}/context_monitor.cpp"
    "${yw1_render}/code_digest.cpp" "${yw1_render}/gl_resolver.cpp"
    "${yw1_render}/gl_api.cpp")
target_include_directories(yw1_render_guards PUBLIC source
    "${YW1_IMGUI_SOURCE_DIR}/backends")
target_compile_features(yw1_render_guards PUBLIC cxx_std_20)
target_compile_options(yw1_render_guards PRIVATE -fPIC -fno-exceptions -fno-rtti -fstack-usage)
target_link_libraries(yw1_render_guards PUBLIC yw1nx_foundation)
set(yw1_safe_backend "${CMAKE_CURRENT_BINARY_DIR}/generated/imgui_impl_opengl3_safe.cpp")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
yw1_patch_imgui("${YW1_IMGUI_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp" "${yw1_safe_backend}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/cmake/imgui_safe_patch.cmake")
add_library(yw1_render_foundation STATIC
    "${YW1_IMGUI_SOURCE_DIR}/imgui.cpp" "${YW1_IMGUI_SOURCE_DIR}/imgui_draw.cpp"
    "${YW1_IMGUI_SOURCE_DIR}/imgui_tables.cpp" "${YW1_IMGUI_SOURCE_DIR}/imgui_widgets.cpp"
    "${YW1_IMGUI_SOURCE_DIR}/imgui_demo.cpp" "${yw1_safe_backend}"
    "${yw1_render}/gl_state.cpp" "${yw1_render}/backend_support.cpp"
    "${yw1_render}/renderer_lease.cpp"
    "${yw1_render}/renderer_pipeline.cpp")
target_include_directories(yw1_render_foundation PUBLIC source "${YW1_IMGUI_SOURCE_DIR}"
    "${YW1_IMGUI_SOURCE_DIR}/backends")
target_compile_features(yw1_render_foundation PUBLIC cxx_std_20)
target_compile_definitions(yw1_render_foundation PRIVATE IMGUI_IMPL_OPENGL_LOADER_CUSTOM)
set_source_files_properties("${yw1_safe_backend}" PROPERTIES
    COMPILE_OPTIONS "-include;${yw1_render}/opengl_loader.hpp")
target_compile_options(yw1_render_foundation PRIVATE -fPIC -fno-exceptions -fno-rtti -fstack-usage
    -ffunction-sections -fdata-sections)
if(YW1_NATIVE_RENDER_EXPERIMENT)
    include(cmake/native_imgui_runtime.cmake)
endif()
target_link_libraries(yw1_render_foundation PUBLIC yw1_render_guards)
