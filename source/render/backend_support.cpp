#include "backend_support.hpp"
#include "imgui.h"

namespace yw1::render {
namespace { constinit std::uint64_t submitted = 0; }
void noteDrawSubmission() { ++submitted; }
std::uint64_t drawSubmissionCount() { return submitted; }
bool bufferStorage(GLenum target, GLsizeiptr expected) {
    GLint observed = -1;
    if (expected < 0 || expected > 16 * 1024 * 1024) {
        failBackend(BackendFailure::BufferStorage); return false;
    }
    activeGl().GetBufferParameteriv(target, 0x8764, &observed); // GL_BUFFER_SIZE
    if (observed != expected) { failBackend(BackendFailure::BufferStorage); return false; }
    return true;
}
void updateTexture(ImTextureData* tex) {
    if (!tex || backendFailure() != BackendFailure::None) { return; }
    auto& gl = activeGl();
    GlStateScope scope(gl);
    if (!scope.valid()) { return; }
    if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0) {
        const auto id = static_cast<GLuint>(tex->TexID);
        if (id) { gl.DeleteTextures(1, &id); }
        tex->SetTexID(ImTextureID_Invalid); tex->SetStatus(ImTextureStatus_Destroyed);
        return;
    }
    if (tex->Status != ImTextureStatus_WantCreate && tex->Status != ImTextureStatus_WantUpdates) {
        return;
    }
    if (tex->Format != ImTextureFormat_RGBA32 || tex->Width <= 0 || tex->Height <= 0 ||
        tex->Width > 4096 || tex->Height > 4096 || !tex->GetPixels()) {
        failBackend(BackendFailure::TextureFormat); return;
    }
    const bool create = tex->Status == ImTextureStatus_WantCreate;
    GLuint id = create ? 0 : static_cast<GLuint>(tex->TexID);
    if (create) { gl.GenTextures(1, &id); }
    if (!id) { failBackend(BackendFailure::Texture); return; }
    bool committed = false;
    ScopeExit cleanup{[&] { if (create && !committed) { gl.DeleteTextures(1, &id); } }};
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl.PixelStorei(GL_UNPACK_ROW_LENGTH, create ? 0 : tex->Width);
    gl.PixelStorei(0x0CF3, 0); gl.PixelStorei(0x0CF4, 0);
    gl.BindTexture(GL_TEXTURE_2D, id);
    if (create) {
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl.TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex->Width, tex->Height, 0,
                      GL_RGBA, GL_UNSIGNED_BYTE, tex->GetPixels());
    } else {
        for (const auto& r : tex->Updates) {
            if (r.x < 0 || r.y < 0 || r.w < 0 || r.h < 0 ||
                r.x + r.w > tex->Width || r.y + r.h > tex->Height) {
                failBackend(BackendFailure::TextureFormat); return;
            }
            gl.TexSubImage2D(GL_TEXTURE_2D, 0, r.x, r.y, r.w, r.h,
                            GL_RGBA, GL_UNSIGNED_BYTE, tex->GetPixelsAt(r.x, r.y));
        }
    }
    GLint width = -1, height = -1;
    gl.GetTexLevelParameteriv(GL_TEXTURE_2D, 0, 0x1000, &width);
    gl.GetTexLevelParameteriv(GL_TEXTURE_2D, 0, 0x1001, &height);
    if (width != tex->Width || height != tex->Height) {
        failBackend(BackendFailure::TextureStorage); return;
    }
    if (!scope.restore()) { return; }
    tex->SetTexID(id); tex->SetStatus(ImTextureStatus_OK); committed = true;
    // storage観測とupload送信だけを確認する。GetError消費やpixel成功の推測はしない。
}
} // 名前空間 yw1::render
