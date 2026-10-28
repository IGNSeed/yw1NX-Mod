#include "gl_state.hpp"
#include <limits>

namespace yw1::render {
namespace {
constinit BackendFailure failure = BackendFailure::None;
struct Query { GLenum name; unsigned count; };
// Desktop GL4.6 compatibility専用。ES/別版へ機械的に流用しない。
constexpr Query kQueries[]{
    {GL_ACTIVE_TEXTURE,1}, {GL_CURRENT_PROGRAM,1}, {GL_VERTEX_ARRAY_BINDING,1},
    {GL_ARRAY_BUFFER_BINDING,1}, {GL_ELEMENT_ARRAY_BUFFER_BINDING,1},
    {GL_VIEWPORT,4}, {GL_SCISSOR_BOX,4}, {GL_POLYGON_MODE,2},
    {GL_BLEND_SRC_RGB,1}, {GL_BLEND_DST_RGB,1}, {GL_BLEND_SRC_ALPHA,1},
    {GL_BLEND_DST_ALPHA,1}, {GL_BLEND_EQUATION_RGB,1}, {GL_BLEND_EQUATION_ALPHA,1},
    {GL_BLEND,1}, {GL_CULL_FACE,1}, {GL_DEPTH_TEST,1}, {GL_STENCIL_TEST,1},
    {GL_SCISSOR_TEST,1}, {GL_PRIMITIVE_RESTART,1},
    {GL_PIXEL_UNPACK_BUFFER_BINDING,1}, {GL_UNPACK_ALIGNMENT,1}, {GL_UNPACK_ROW_LENGTH,1},
    {0x0CF3,1}, {0x0CF4,1}, // UNPACK_SKIP_ROWS / PIXELS
    {0x8CA6,1}, {0x8CAA,1}, // DRAW / READ FRAMEBUFFER_BINDING（変更しない）
    {0x0C23,4}, {0x0B72,1}, {0x0B98,1}, {0x8CA5,1}, // 書込mask（変更しない）
    {0x8C89,1}, {0x8E24,1} // RASTERIZER_DISCARD / TRANSFORM_FEEDBACK_ACTIVE
};
constexpr unsigned offset(unsigned query) {
    unsigned n = 0;
    for (unsigned i = 0; i < query; ++i) { n += kQueries[i].count; }
    return n;
}
constexpr unsigned kTexture = offset(std::size(kQueries)), kSampler = kTexture + 1;
static_assert(kSampler < 64);
constexpr GLint kUnwritten = (std::numeric_limits<GLint>::min)();
} // 無名名前空間
void clearBackendFailure() { failure = BackendFailure::None; }
void failBackend(BackendFailure reason) { if (failure == BackendFailure::None) { failure = reason; } }
BackendFailure backendFailure() { return failure; }
bool GlStateScope::capture(GlState& state) {
    state.values.fill(0);
    for (unsigned i = 0; i < std::size(kQueries); ++i) {
        auto* values = state.values.data() + offset(i);
        for (unsigned j = 0; j < kQueries[i].count; ++j) { values[j] = kUnwritten; }
        api_.GetIntegerv(kQueries[i].name, values);
        for (unsigned j = 0; j < kQueries[i].count; ++j) {
            if (values[j] == kUnwritten) { return false; }
        }
    }
    const auto active = state.values[0];
    if (active < static_cast<GLint>(GL_TEXTURE0) || active > static_cast<GLint>(GL_TEXTURE0 + 31)) {
        return false;
    }
    api_.ActiveTexture(GL_TEXTURE0);
    state.values[kTexture] = state.values[kSampler] = kUnwritten;
    api_.GetIntegerv(GL_TEXTURE_BINDING_2D, &state.values[kTexture]);
    api_.GetIntegerv(GL_SAMPLER_BINDING, &state.values[kSampler]);
    // 途中のquery失敗でも、この関数自身のactive unit変更は必ず戻す。
    api_.ActiveTexture(static_cast<GLenum>(active));
    return state.values[kTexture] != kUnwritten && state.values[kSampler] != kUnwritten;
}
GlStateScope::GlStateScope(GlApi& api) : api_(api) {
    if (!api_.GetString || !api_.GetIntegerv || !api_.ActiveTexture) {
        failBackend(BackendFailure::StateCapture); return;
    }
    const auto* version = api_.GetString(GL_VERSION);
    if (!version || version[0] != '4' || version[1] != '.' || version[2] != '6' ||
        (version[3] && version[3] != '.' && version[3] != ' ')) {
        failBackend(BackendFailure::StateCapture); return;
    }
    GLint major = -1, minor = -1, profile = -1;
    api_.GetIntegerv(GL_MAJOR_VERSION, &major); api_.GetIntegerv(GL_MINOR_VERSION, &minor);
    api_.GetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
    if (major != 4 || minor != 6 || profile != 2 || !capture(saved_)) {
        failBackend(BackendFailure::StateCapture); return;
    }
    const auto& v = saved_.values;
    if (v[offset(31)] || v[offset(32)]) {
        // 有効なtransform feedback等を無断で中断しない。
        failBackend(BackendFailure::StateCapture); return;
    }
    if (v[1]) {
        GLint deleting = kUnwritten;
        if (!api_.IsProgram(static_cast<GLuint>(v[1]))) {
            failBackend(BackendFailure::StateCapture); return;
        }
        api_.GetProgramiv(static_cast<GLuint>(v[1]), 0x8B80, &deleting); // GL_DELETE_STATUS
        if (deleting != GL_FALSE) { failBackend(BackendFailure::StateCapture); return; }
    }
    valid_ = true;
}
bool GlStateScope::restore() {
    if (!valid_) { return false; }
    if (restored_) { return true; }
    const auto& v = saved_.values;
    api_.UseProgram(static_cast<GLuint>(v[1]));
    api_.ActiveTexture(GL_TEXTURE0);
    api_.BindTexture(GL_TEXTURE_2D, static_cast<GLuint>(v[kTexture]));
    api_.BindSampler(0, static_cast<GLuint>(v[kSampler]));
    api_.BindVertexArray(static_cast<GLuint>(v[2]));
    api_.BindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(v[3]));
    api_.BindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(v[offset(20)]));
    for (unsigned i = 21; i <= 24; ++i) {
        api_.PixelStorei(kQueries[i].name, v[offset(i)]);
    }
    api_.BlendFuncSeparate(v[offset(8)], v[offset(9)], v[offset(10)], v[offset(11)]);
    api_.BlendEquationSeparate(v[offset(12)], v[offset(13)]);
    for (unsigned i = 14; i <= 19; ++i) {
        if (v[offset(i)]) { api_.Enable(kQueries[i].name); }
        else { api_.Disable(kQueries[i].name); }
    }
    api_.PolygonMode(GL_FRONT, v[offset(7)]);
    api_.PolygonMode(GL_BACK, v[offset(7) + 1]);
    api_.Viewport(v[5], v[6], v[7], v[8]);
    api_.Scissor(v[9], v[10], v[11], v[12]);
    api_.ActiveTexture(v[0]);
    GlState after{};
    restored_ = capture(after) && after == saved_;
    if (!restored_) { failBackend(BackendFailure::StateRestore); }
    return restored_;
}
GlStateScope::~GlStateScope() {
    if (valid_ && !restored_) { static_cast<void>(restore()); }
}
} // 名前空間 yw1::render
