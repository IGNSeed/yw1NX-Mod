# 上流原本は保持し、固定版への限定変更だけをbuild領域へ適用する。
function(yw1_replace_once variable before after)
    string(FIND "${${variable}}" "${before}" position)
    if(position LESS 0)
        message(FATAL_ERROR "Pinned ImGui patch anchor was not found")
    endif()
    string(LENGTH "${before}" length)
    math(EXPR tail "${position} + ${length}")
    string(SUBSTRING "${${variable}}" ${tail} -1 remainder)
    string(FIND "${remainder}" "${before}" duplicate)
    if(NOT duplicate LESS 0)
        message(FATAL_ERROR "Pinned ImGui patch anchor was not unique")
    endif()
    string(REPLACE "${before}" "${after}" changed "${${variable}}")
    set(${variable} "${changed}" PARENT_SCOPE)
endfunction()
function(yw1_replace_section variable start finish replacement)
    string(FIND "${${variable}}" "${start}" begin)
    if(begin LESS 0)
        message(FATAL_ERROR "Pinned ImGui section start missing")
    endif()
    string(SUBSTRING "${${variable}}" ${begin} -1 body)
    string(FIND "${body}" "${finish}" end)
    if(end LESS 0)
        message(FATAL_ERROR "Pinned ImGui section end missing")
    endif()
    string(SUBSTRING "${${variable}}" 0 ${begin} prefix)
    string(SUBSTRING "${body}" ${end} -1 suffix)
    set(${variable} "${prefix}${replacement}${suffix}" PARENT_SCOPE)
endfunction()
function(yw1_patch_imgui input output)
    file(READ "${input}" source)
    # Shader失敗のログ用allocationを避け、実statusだけを返す。
    yw1_replace_section(source "static bool CheckShader(GLuint handle, const char* desc)"
        "static bool CheckProgram(GLuint handle, const char* desc)" [=[static bool CheckShader(GLuint handle, const char* desc)
{
    (void)desc;
    GLint status = -1;
    glGetShaderiv(handle, GL_COMPILE_STATUS, &status);
    return handle != 0 && status == GL_TRUE;
}

]=])
    yw1_replace_section(source "static bool CheckProgram(GLuint handle, const char* desc)"
        "bool    ImGui_ImplOpenGL3_CreateDeviceObjects()" [=[static bool CheckProgram(GLuint handle, const char* desc)
{
    (void)desc;
    GLint status = -1;
    glGetProgramiv(handle, GL_LINK_STATUS, &status);
    return handle != 0 && status == GL_TRUE;
}

]=])
    string(FIND "${source}" "bool    ImGui_ImplOpenGL3_CreateDeviceObjects()" begin)
    string(SUBSTRING "${source}" 0 ${begin} prefix)
    string(SUBSTRING "${source}" ${begin} -1 device)
    string(FIND "${device}" "void    ImGui_ImplOpenGL3_DestroyDeviceObjects()" ending)
    string(SUBSTRING "${device}" ${ending} -1 suffix)
    string(SUBSTRING "${device}" 0 ${ending} device)
    yw1_replace_section(device "    // Backup GL state" "    // Parse GLSL version string" [=[    yw1::render::GlStateScope state(yw1::render::activeGl());
    if (!state.valid()) return false;
    GLuint vert_handle = 0, frag_handle = 0;
    bool committed = false;
    yw1::render::ScopeExit cleanup{[&] {
        if (committed) return;
        if (bd->ShaderHandle) glDeleteProgram(bd->ShaderHandle);
        if (vert_handle) glDeleteShader(vert_handle);
        if (frag_handle) glDeleteShader(frag_handle);
        if (bd->VboHandle) glDeleteBuffers(1, &bd->VboHandle);
        if (bd->ElementsHandle) glDeleteBuffers(1, &bd->ElementsHandle);
        if (bd->TexSamplers[0] || bd->TexSamplers[1]) glDeleteSamplers(2, bd->TexSamplers);
        bd->ShaderHandle = bd->VboHandle = bd->ElementsHandle = 0;
        bd->TexSamplers[0] = bd->TexSamplers[1] = 0;
    }};
]=])
    yw1_replace_once(device "    GLuint vert_handle;" "")
    yw1_replace_once(device "    GLuint frag_handle;" "")
    yw1_replace_once(device "    GL_CALL(vert_handle = glCreateShader(GL_VERTEX_SHADER));"
        "    GL_CALL(vert_handle = glCreateShader(GL_VERTEX_SHADER));\n    if (!vert_handle) { yw1::render::failBackend(yw1::render::BackendFailure::VertexShader); return false; }")
    yw1_replace_once(device "    GL_CALL(frag_handle = glCreateShader(GL_FRAGMENT_SHADER));"
        "    GL_CALL(frag_handle = glCreateShader(GL_FRAGMENT_SHADER));\n    if (!frag_handle) { yw1::render::failBackend(yw1::render::BackendFailure::FragmentShader); return false; }")
    yw1_replace_once(device "    bd->ShaderHandle = glCreateProgram();"
        "    bd->ShaderHandle = glCreateProgram();\n    if (!bd->ShaderHandle) { yw1::render::failBackend(yw1::render::BackendFailure::Program); return false; }")
    yw1_replace_once(device "    if (!CheckShader(vert_handle, \"vertex shader\"))\n        return false;"
        "    if (!CheckShader(vert_handle, \"vertex shader\")) { yw1::render::failBackend(yw1::render::BackendFailure::VertexShader); return false; }")
    yw1_replace_once(device "    if (!CheckShader(frag_handle, \"fragment shader\"))\n        return false;"
        "    if (!CheckShader(frag_handle, \"fragment shader\")) { yw1::render::failBackend(yw1::render::BackendFailure::FragmentShader); return false; }")
    yw1_replace_once(device "    if (!CheckProgram(bd->ShaderHandle, \"shader program\"))\n        return false;"
        "    if (!CheckProgram(bd->ShaderHandle, \"shader program\")) { yw1::render::failBackend(yw1::render::BackendFailure::Program); return false; }")
    yw1_replace_once(device "    glDeleteShader(vert_handle);" "    glDeleteShader(vert_handle); vert_handle = 0;")
    yw1_replace_once(device "    glDeleteShader(frag_handle);" "    glDeleteShader(frag_handle); frag_handle = 0;")
    yw1_replace_once(device "    // Create buffers" [=[    if (bd->AttribLocationTex < 0 || bd->AttribLocationProjMtx < 0 ||
        (GLint)bd->AttribLocationVtxPos < 0 || (GLint)bd->AttribLocationVtxUV < 0 ||
        (GLint)bd->AttribLocationVtxColor < 0) {
        yw1::render::failBackend(yw1::render::BackendFailure::Locations); return false;
    }
    // Create buffers]=])
    yw1_replace_once(device "    glGenBuffers(1, &bd->ElementsHandle);" [=[    glGenBuffers(1, &bd->ElementsHandle);
    if (!bd->VboHandle || !bd->ElementsHandle) {
        yw1::render::failBackend(yw1::render::BackendFailure::Buffer); return false;
    }]=])
    yw1_replace_once(device "        glGenSamplers(2, &bd->TexSamplers[0]);" [=[        glGenSamplers(2, &bd->TexSamplers[0]);
        if (!bd->TexSamplers[0] || !bd->TexSamplers[1]) {
            yw1::render::failBackend(yw1::render::BackendFailure::Buffer); return false;
        }]=])
    yw1_replace_section(device "    // Restore modified GL state" "    return true;" [=[    if (!state.restore()) return false;
    committed = true;
]=])
    set(source "${prefix}${device}${suffix}")
    # Texture作成・更新・破棄は所有物と全終了経路を扱う自作adapterへ委譲する。
    yw1_replace_section(source "void ImGui_ImplOpenGL3_UpdateTexture(ImTextureData* tex)"
        "// If you get an error please report on github." [=[void ImGui_ImplOpenGL3_UpdateTexture(ImTextureData* tex)
{
    yw1::render::updateTexture(tex);
}

]=])
    string(FIND "${source}" "void    ImGui_ImplOpenGL3_RenderDrawData(ImDrawData* draw_data)" begin)
    string(SUBSTRING "${source}" 0 ${begin} prefix)
    string(SUBSTRING "${source}" ${begin} -1 draw)
    string(FIND "${draw}" "static void ImGui_ImplOpenGL3_DestroyTexture" ending)
    string(SUBSTRING "${draw}" ${ending} -1 suffix)
    string(SUBSTRING "${draw}" 0 ${ending} draw)
    yw1_replace_once(draw "    // Catch up with texture updates." [=[    yw1::render::GlStateScope state(yw1::render::activeGl());
    if (!state.valid()) return;
    glActiveTexture(GL_TEXTURE0);
    // Catch up with texture updates.]=])
    yw1_replace_section(draw "    // Backup GL state" "    // Support for GL 4.5" [=[    if (yw1::render::backendFailure() != yw1::render::BackendFailure::None) return;
]=])
    yw1_replace_once(draw "    GL_CALL(glGenVertexArrays(1, &vertex_array_object));" [=[    GL_CALL(glGenVertexArrays(1, &vertex_array_object));
    if (!vertex_array_object) { yw1::render::failBackend(yw1::render::BackendFailure::VertexArray); return; }
    yw1::render::ScopeExit vao_cleanup{[&] {
        ImGui::GetPlatformIO().Renderer_RenderState = nullptr;
        glDeleteVertexArrays(1, &vertex_array_object);
    }};]=])
    yw1_replace_once(draw "        for (int cmd_i = 0; cmd_i < draw_list->CmdBuffer.Size; cmd_i++)" [=[        if (!yw1::render::bufferStorage(GL_ARRAY_BUFFER, vtx_buffer_size) ||
            !yw1::render::bufferStorage(GL_ELEMENT_ARRAY_BUFFER, idx_buffer_size)) return;

        for (int cmd_i = 0; cmd_i < draw_list->CmdBuffer.Size; cmd_i++)]=])
    string(FIND "${draw}" "    // Destroy the temporary VAO" finish)
    if(finish LESS 0)
        message(FATAL_ERROR "Pinned renderer cleanup anchor missing")
    endif()
    string(SUBSTRING "${draw}" 0 ${finish} draw)
    string(APPEND draw "}\n\n")
    yw1_replace_once(draw "GL_CALL(glDrawElementsBaseVertex(" "YW1_GL_DRAW(glDrawElementsBaseVertex(")
    yw1_replace_once(draw "GL_CALL(glDrawElements(" "YW1_GL_DRAW(glDrawElements(")
    set(source "${prefix}${draw}${suffix}")
    file(WRITE "${output}" "// YW1限定adapter適用版。原本とMIT表記は保持。\n#include \"render/backend_support.hpp\"\n#define YW1_GL_DRAW(...) do { __VA_ARGS__; yw1::render::noteDrawSubmission(); } while (0)\n${source}")
endfunction()
