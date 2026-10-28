#include "gl_api.hpp"

namespace yw1::render {
namespace {
constinit GlApi* current = nullptr;
}
GlApi& activeGl() { return *current; }
void bindGl(GlApi* api) { current = api; }
bool GlApi::assign(const ResolveStats& proof, std::span<const GlProc> pointers) {
    if (pointers.size() != 61) { return false; }
    *this = {};
    if (proof.verifiedMask != 0x1FFFFFFDFFFFFFFFULL || proof.rejectedMask) { return false; }
    if (proof.result != ResolveResult::Ready || proof.verified != 60 || proof.attempted != 60 || proof.required != 60 || proof.rejected) { return false; }
    if (!pointers[0]) { return false; }
    if (!pointers[1]) { return false; }
    if (!pointers[2]) { return false; }
    if (!pointers[3]) { return false; }
    if (!pointers[4]) { return false; }
    if (!pointers[5]) { return false; }
    if (!pointers[6]) { return false; }
    if (!pointers[7]) { return false; }
    if (!pointers[8]) { return false; }
    if (!pointers[9]) { return false; }
    if (!pointers[10]) { return false; }
    if (!pointers[11]) { return false; }
    if (!pointers[12]) { return false; }
    if (!pointers[13]) { return false; }
    if (!pointers[14]) { return false; }
    if (!pointers[15]) { return false; }
    if (!pointers[16]) { return false; }
    if (!pointers[17]) { return false; }
    if (!pointers[18]) { return false; }
    if (!pointers[19]) { return false; }
    if (!pointers[20]) { return false; }
    if (!pointers[21]) { return false; }
    if (!pointers[22]) { return false; }
    if (!pointers[23]) { return false; }
    if (!pointers[24]) { return false; }
    if (!pointers[25]) { return false; }
    if (!pointers[26]) { return false; }
    if (!pointers[27]) { return false; }
    if (!pointers[28]) { return false; }
    if (!pointers[29]) { return false; }
    if (!pointers[30]) { return false; }
    if (!pointers[31]) { return false; }
    if (!pointers[32]) { return false; }
    if (pointers[33]) { return false; }
    if (!pointers[34]) { return false; }
    if (!pointers[35]) { return false; }
    if (!pointers[36]) { return false; }
    if (!pointers[37]) { return false; }
    if (!pointers[38]) { return false; }
    if (!pointers[39]) { return false; }
    if (!pointers[40]) { return false; }
    if (!pointers[41]) { return false; }
    if (!pointers[42]) { return false; }
    if (!pointers[43]) { return false; }
    if (!pointers[44]) { return false; }
    if (!pointers[45]) { return false; }
    if (!pointers[46]) { return false; }
    if (!pointers[47]) { return false; }
    if (!pointers[48]) { return false; }
    if (!pointers[49]) { return false; }
    if (!pointers[50]) { return false; }
    if (!pointers[51]) { return false; }
    if (!pointers[52]) { return false; }
    if (!pointers[53]) { return false; }
    if (!pointers[54]) { return false; }
    if (!pointers[55]) { return false; }
    if (!pointers[56]) { return false; }
    if (!pointers[57]) { return false; }
    if (!pointers[58]) { return false; }
    if (!pointers[59]) { return false; }
    if (!pointers[60]) { return false; }
    ActiveTexture = reinterpret_cast<PFNGLACTIVETEXTUREPROC>(pointers[0]);
    AttachShader = reinterpret_cast<PFNGLATTACHSHADERPROC>(pointers[1]);
    BindBuffer = reinterpret_cast<PFNGLBINDBUFFERPROC>(pointers[2]);
    BindSampler = reinterpret_cast<PFNGLBINDSAMPLERPROC>(pointers[3]);
    BindTexture = reinterpret_cast<PFNGLBINDTEXTUREPROC>(pointers[4]);
    BindVertexArray = reinterpret_cast<PFNGLBINDVERTEXARRAYPROC>(pointers[5]);
    BlendEquation = reinterpret_cast<PFNGLBLENDEQUATIONPROC>(pointers[6]);
    BlendEquationSeparate = reinterpret_cast<PFNGLBLENDEQUATIONSEPARATEPROC>(pointers[7]);
    BlendFuncSeparate = reinterpret_cast<PFNGLBLENDFUNCSEPARATEPROC>(pointers[8]);
    BufferData = reinterpret_cast<PFNGLBUFFERDATAPROC>(pointers[9]);
    BufferSubData = reinterpret_cast<PFNGLBUFFERSUBDATAPROC>(pointers[10]);
    CompileShader = reinterpret_cast<PFNGLCOMPILESHADERPROC>(pointers[11]);
    CreateProgram = reinterpret_cast<PFNGLCREATEPROGRAMPROC>(pointers[12]);
    CreateShader = reinterpret_cast<PFNGLCREATESHADERPROC>(pointers[13]);
    DeleteBuffers = reinterpret_cast<PFNGLDELETEBUFFERSPROC>(pointers[14]);
    DeleteProgram = reinterpret_cast<PFNGLDELETEPROGRAMPROC>(pointers[15]);
    DeleteSamplers = reinterpret_cast<PFNGLDELETESAMPLERSPROC>(pointers[16]);
    DeleteShader = reinterpret_cast<PFNGLDELETESHADERPROC>(pointers[17]);
    DeleteTextures = reinterpret_cast<PFNGLDELETETEXTURESPROC>(pointers[18]);
    DeleteVertexArrays = reinterpret_cast<PFNGLDELETEVERTEXARRAYSPROC>(pointers[19]);
    DetachShader = reinterpret_cast<PFNGLDETACHSHADERPROC>(pointers[20]);
    Disable = reinterpret_cast<PFNGLDISABLEPROC>(pointers[21]);
    DisableVertexAttribArray = reinterpret_cast<PFNGLDISABLEVERTEXATTRIBARRAYPROC>(pointers[22]);
    DrawElements = reinterpret_cast<PFNGLDRAWELEMENTSPROC>(pointers[23]);
    DrawElementsBaseVertex = reinterpret_cast<PFNGLDRAWELEMENTSBASEVERTEXPROC>(pointers[24]);
    Enable = reinterpret_cast<PFNGLENABLEPROC>(pointers[25]);
    EnableVertexAttribArray = reinterpret_cast<PFNGLENABLEVERTEXATTRIBARRAYPROC>(pointers[26]);
    GenBuffers = reinterpret_cast<PFNGLGENBUFFERSPROC>(pointers[27]);
    GenSamplers = reinterpret_cast<PFNGLGENSAMPLERSPROC>(pointers[28]);
    GenTextures = reinterpret_cast<PFNGLGENTEXTURESPROC>(pointers[29]);
    GenVertexArrays = reinterpret_cast<PFNGLGENVERTEXARRAYSPROC>(pointers[30]);
    GetAttribLocation = reinterpret_cast<PFNGLGETATTRIBLOCATIONPROC>(pointers[31]);
    GetBufferParameteriv = reinterpret_cast<GetBufferParameter>(pointers[32]);
    GetError = reinterpret_cast<PFNGLGETERRORPROC>(pointers[33]);
    GetIntegerv = reinterpret_cast<PFNGLGETINTEGERVPROC>(pointers[34]);
    GetProgramInfoLog = reinterpret_cast<PFNGLGETPROGRAMINFOLOGPROC>(pointers[35]);
    GetProgramiv = reinterpret_cast<PFNGLGETPROGRAMIVPROC>(pointers[36]);
    GetShaderInfoLog = reinterpret_cast<PFNGLGETSHADERINFOLOGPROC>(pointers[37]);
    GetShaderiv = reinterpret_cast<PFNGLGETSHADERIVPROC>(pointers[38]);
    GetString = reinterpret_cast<PFNGLGETSTRINGPROC>(pointers[39]);
    GetStringi = reinterpret_cast<PFNGLGETSTRINGIPROC>(pointers[40]);
    GetTexLevelParameteriv = reinterpret_cast<GetTextureLevelParameter>(pointers[41]);
    GetUniformLocation = reinterpret_cast<PFNGLGETUNIFORMLOCATIONPROC>(pointers[42]);
    GetVertexAttribPointerv = reinterpret_cast<PFNGLGETVERTEXATTRIBPOINTERVPROC>(pointers[43]);
    GetVertexAttribiv = reinterpret_cast<PFNGLGETVERTEXATTRIBIVPROC>(pointers[44]);
    IsEnabled = reinterpret_cast<PFNGLISENABLEDPROC>(pointers[45]);
    IsProgram = reinterpret_cast<PFNGLISPROGRAMPROC>(pointers[46]);
    LinkProgram = reinterpret_cast<PFNGLLINKPROGRAMPROC>(pointers[47]);
    PixelStorei = reinterpret_cast<PFNGLPIXELSTOREIPROC>(pointers[48]);
    PolygonMode = reinterpret_cast<PFNGLPOLYGONMODEPROC>(pointers[49]);
    SamplerParameteri = reinterpret_cast<PFNGLSAMPLERPARAMETERIPROC>(pointers[50]);
    Scissor = reinterpret_cast<PFNGLSCISSORPROC>(pointers[51]);
    ShaderSource = reinterpret_cast<PFNGLSHADERSOURCEPROC>(pointers[52]);
    TexImage2D = reinterpret_cast<PFNGLTEXIMAGE2DPROC>(pointers[53]);
    TexParameteri = reinterpret_cast<PFNGLTEXPARAMETERIPROC>(pointers[54]);
    TexSubImage2D = reinterpret_cast<PFNGLTEXSUBIMAGE2DPROC>(pointers[55]);
    Uniform1i = reinterpret_cast<PFNGLUNIFORM1IPROC>(pointers[56]);
    UniformMatrix4fv = reinterpret_cast<PFNGLUNIFORMMATRIX4FVPROC>(pointers[57]);
    UseProgram = reinterpret_cast<PFNGLUSEPROGRAMPROC>(pointers[58]);
    VertexAttribPointer = reinterpret_cast<PFNGLVERTEXATTRIBPOINTERPROC>(pointers[59]);
    Viewport = reinterpret_cast<PFNGLVIEWPORTPROC>(pointers[60]);
    return true;
}
}
