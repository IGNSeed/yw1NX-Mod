#pragma once
#include <imgui_impl_opengl3_loader.h>
#include "gl_resolver.hpp"

namespace yw1::render {
using GetBufferParameter = void (APIENTRY *)(GLenum, GLenum, GLint*);
using GetTextureLevelParameter = void (APIENTRY *)(GLenum, GLint, GLenum, GLint*);
struct GlApi {
    PFNGLACTIVETEXTUREPROC ActiveTexture = nullptr;
    PFNGLATTACHSHADERPROC AttachShader = nullptr;
    PFNGLBINDBUFFERPROC BindBuffer = nullptr;
    PFNGLBINDSAMPLERPROC BindSampler = nullptr;
    PFNGLBINDTEXTUREPROC BindTexture = nullptr;
    PFNGLBINDVERTEXARRAYPROC BindVertexArray = nullptr;
    PFNGLBLENDEQUATIONPROC BlendEquation = nullptr;
    PFNGLBLENDEQUATIONSEPARATEPROC BlendEquationSeparate = nullptr;
    PFNGLBLENDFUNCSEPARATEPROC BlendFuncSeparate = nullptr;
    PFNGLBUFFERDATAPROC BufferData = nullptr;
    PFNGLBUFFERSUBDATAPROC BufferSubData = nullptr;
    PFNGLCOMPILESHADERPROC CompileShader = nullptr;
    PFNGLCREATEPROGRAMPROC CreateProgram = nullptr;
    PFNGLCREATESHADERPROC CreateShader = nullptr;
    PFNGLDELETEBUFFERSPROC DeleteBuffers = nullptr;
    PFNGLDELETEPROGRAMPROC DeleteProgram = nullptr;
    PFNGLDELETESAMPLERSPROC DeleteSamplers = nullptr;
    PFNGLDELETESHADERPROC DeleteShader = nullptr;
    PFNGLDELETETEXTURESPROC DeleteTextures = nullptr;
    PFNGLDELETEVERTEXARRAYSPROC DeleteVertexArrays = nullptr;
    PFNGLDETACHSHADERPROC DetachShader = nullptr;
    PFNGLDISABLEPROC Disable = nullptr;
    PFNGLDISABLEVERTEXATTRIBARRAYPROC DisableVertexAttribArray = nullptr;
    PFNGLDRAWELEMENTSPROC DrawElements = nullptr;
    PFNGLDRAWELEMENTSBASEVERTEXPROC DrawElementsBaseVertex = nullptr;
    PFNGLENABLEPROC Enable = nullptr;
    PFNGLENABLEVERTEXATTRIBARRAYPROC EnableVertexAttribArray = nullptr;
    PFNGLGENBUFFERSPROC GenBuffers = nullptr;
    PFNGLGENSAMPLERSPROC GenSamplers = nullptr;
    PFNGLGENTEXTURESPROC GenTextures = nullptr;
    PFNGLGENVERTEXARRAYSPROC GenVertexArrays = nullptr;
    PFNGLGETATTRIBLOCATIONPROC GetAttribLocation = nullptr;
    GetBufferParameter GetBufferParameteriv = nullptr;
    PFNGLGETERRORPROC GetError = nullptr;
    PFNGLGETINTEGERVPROC GetIntegerv = nullptr;
    PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog = nullptr;
    PFNGLGETPROGRAMIVPROC GetProgramiv = nullptr;
    PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog = nullptr;
    PFNGLGETSHADERIVPROC GetShaderiv = nullptr;
    PFNGLGETSTRINGPROC GetString = nullptr;
    PFNGLGETSTRINGIPROC GetStringi = nullptr;
    GetTextureLevelParameter GetTexLevelParameteriv = nullptr;
    PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation = nullptr;
    PFNGLGETVERTEXATTRIBPOINTERVPROC GetVertexAttribPointerv = nullptr;
    PFNGLGETVERTEXATTRIBIVPROC GetVertexAttribiv = nullptr;
    PFNGLISENABLEDPROC IsEnabled = nullptr;
    PFNGLISPROGRAMPROC IsProgram = nullptr;
    PFNGLLINKPROGRAMPROC LinkProgram = nullptr;
    PFNGLPIXELSTOREIPROC PixelStorei = nullptr;
    PFNGLPOLYGONMODEPROC PolygonMode = nullptr;
    PFNGLSAMPLERPARAMETERIPROC SamplerParameteri = nullptr;
    PFNGLSCISSORPROC Scissor = nullptr;
    PFNGLSHADERSOURCEPROC ShaderSource = nullptr;
    PFNGLTEXIMAGE2DPROC TexImage2D = nullptr;
    PFNGLTEXPARAMETERIPROC TexParameteri = nullptr;
    PFNGLTEXSUBIMAGE2DPROC TexSubImage2D = nullptr;
    PFNGLUNIFORM1IPROC Uniform1i = nullptr;
    PFNGLUNIFORMMATRIX4FVPROC UniformMatrix4fv = nullptr;
    PFNGLUSEPROGRAMPROC UseProgram = nullptr;
    PFNGLVERTEXATTRIBPOINTERPROC VertexAttribPointer = nullptr;
    PFNGLVIEWPORTPROC Viewport = nullptr;
    bool assign(const ResolveStats& proof, std::span<const GlProc> pointers);
};
GlApi& activeGl();
void bindGl(GlApi* api);
}
