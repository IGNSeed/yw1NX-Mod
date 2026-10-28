#pragma once
#include "gl_api.hpp"
#include "scope_exit.hpp"
#include <array>

namespace yw1::render {
enum class BackendFailure : std::uint32_t {
    None, StateCapture, StateRestore, VertexShader, FragmentShader, Program,
    Locations, Buffer, VertexArray, Texture, TextureStorage, BufferStorage, TextureFormat
};
void clearBackendFailure();
void failBackend(BackendFailure reason);
BackendFailure backendFailure();
struct GlState {
    std::array<GLint, 64> values{};
    bool operator==(const GlState&) const = default;
};
class GlStateScope {
  public:
    explicit GlStateScope(GlApi& api);
    ~GlStateScope();
    bool valid() const { return valid_; }
    bool restore();
    GlStateScope(const GlStateScope&) = delete;
    GlStateScope& operator=(const GlStateScope&) = delete;
  private:
    bool capture(GlState& state);
    GlApi& api_;
    GlState saved_{};
    bool valid_ = false, restored_ = false;
};
} // 名前空間 yw1::render
