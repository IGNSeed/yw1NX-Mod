#pragma once
#include "gl_state.hpp"
struct ImTextureData;

namespace yw1::render {
void updateTexture(ImTextureData* texture);
bool bufferStorage(GLenum target, GLsizeiptr expected);
void noteDrawSubmission();
std::uint64_t drawSubmissionCount();
} // 名前空間 yw1::render
