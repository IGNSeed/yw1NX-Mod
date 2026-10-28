#pragma once
#include "gl_api.hpp"
#undef glActiveTexture
#define glActiveTexture (::yw1::render::activeGl().ActiveTexture)
#undef glAttachShader
#define glAttachShader (::yw1::render::activeGl().AttachShader)
#undef glBindBuffer
#define glBindBuffer (::yw1::render::activeGl().BindBuffer)
#undef glBindSampler
#define glBindSampler (::yw1::render::activeGl().BindSampler)
#undef glBindTexture
#define glBindTexture (::yw1::render::activeGl().BindTexture)
#undef glBindVertexArray
#define glBindVertexArray (::yw1::render::activeGl().BindVertexArray)
#undef glBlendEquation
#define glBlendEquation (::yw1::render::activeGl().BlendEquation)
#undef glBlendEquationSeparate
#define glBlendEquationSeparate (::yw1::render::activeGl().BlendEquationSeparate)
#undef glBlendFuncSeparate
#define glBlendFuncSeparate (::yw1::render::activeGl().BlendFuncSeparate)
#undef glBufferData
#define glBufferData (::yw1::render::activeGl().BufferData)
#undef glBufferSubData
#define glBufferSubData (::yw1::render::activeGl().BufferSubData)
#undef glCompileShader
#define glCompileShader (::yw1::render::activeGl().CompileShader)
#undef glCreateProgram
#define glCreateProgram (::yw1::render::activeGl().CreateProgram)
#undef glCreateShader
#define glCreateShader (::yw1::render::activeGl().CreateShader)
#undef glDeleteBuffers
#define glDeleteBuffers (::yw1::render::activeGl().DeleteBuffers)
#undef glDeleteProgram
#define glDeleteProgram (::yw1::render::activeGl().DeleteProgram)
#undef glDeleteSamplers
#define glDeleteSamplers (::yw1::render::activeGl().DeleteSamplers)
#undef glDeleteShader
#define glDeleteShader (::yw1::render::activeGl().DeleteShader)
#undef glDeleteTextures
#define glDeleteTextures (::yw1::render::activeGl().DeleteTextures)
#undef glDeleteVertexArrays
#define glDeleteVertexArrays (::yw1::render::activeGl().DeleteVertexArrays)
#undef glDetachShader
#define glDetachShader (::yw1::render::activeGl().DetachShader)
#undef glDisable
#define glDisable (::yw1::render::activeGl().Disable)
#undef glDisableVertexAttribArray
#define glDisableVertexAttribArray (::yw1::render::activeGl().DisableVertexAttribArray)
#undef glDrawElements
#define glDrawElements (::yw1::render::activeGl().DrawElements)
#undef glDrawElementsBaseVertex
#define glDrawElementsBaseVertex (::yw1::render::activeGl().DrawElementsBaseVertex)
#undef glEnable
#define glEnable (::yw1::render::activeGl().Enable)
#undef glEnableVertexAttribArray
#define glEnableVertexAttribArray (::yw1::render::activeGl().EnableVertexAttribArray)
#undef glGenBuffers
#define glGenBuffers (::yw1::render::activeGl().GenBuffers)
#undef glGenSamplers
#define glGenSamplers (::yw1::render::activeGl().GenSamplers)
#undef glGenTextures
#define glGenTextures (::yw1::render::activeGl().GenTextures)
#undef glGenVertexArrays
#define glGenVertexArrays (::yw1::render::activeGl().GenVertexArrays)
#undef glGetAttribLocation
#define glGetAttribLocation (::yw1::render::activeGl().GetAttribLocation)
#undef glGetBufferParameteriv
#define glGetBufferParameteriv (::yw1::render::activeGl().GetBufferParameteriv)
#undef glGetError
#define glGetError (::yw1::render::activeGl().GetError)
#undef glGetIntegerv
#define glGetIntegerv (::yw1::render::activeGl().GetIntegerv)
#undef glGetProgramInfoLog
#define glGetProgramInfoLog (::yw1::render::activeGl().GetProgramInfoLog)
#undef glGetProgramiv
#define glGetProgramiv (::yw1::render::activeGl().GetProgramiv)
#undef glGetShaderInfoLog
#define glGetShaderInfoLog (::yw1::render::activeGl().GetShaderInfoLog)
#undef glGetShaderiv
#define glGetShaderiv (::yw1::render::activeGl().GetShaderiv)
#undef glGetString
#define glGetString (::yw1::render::activeGl().GetString)
#undef glGetStringi
#define glGetStringi (::yw1::render::activeGl().GetStringi)
#undef glGetTexLevelParameteriv
#define glGetTexLevelParameteriv (::yw1::render::activeGl().GetTexLevelParameteriv)
#undef glGetUniformLocation
#define glGetUniformLocation (::yw1::render::activeGl().GetUniformLocation)
#undef glGetVertexAttribPointerv
#define glGetVertexAttribPointerv (::yw1::render::activeGl().GetVertexAttribPointerv)
#undef glGetVertexAttribiv
#define glGetVertexAttribiv (::yw1::render::activeGl().GetVertexAttribiv)
#undef glIsEnabled
#define glIsEnabled (::yw1::render::activeGl().IsEnabled)
#undef glIsProgram
#define glIsProgram (::yw1::render::activeGl().IsProgram)
#undef glLinkProgram
#define glLinkProgram (::yw1::render::activeGl().LinkProgram)
#undef glPixelStorei
#define glPixelStorei (::yw1::render::activeGl().PixelStorei)
#undef glPolygonMode
#define glPolygonMode (::yw1::render::activeGl().PolygonMode)
#undef glSamplerParameteri
#define glSamplerParameteri (::yw1::render::activeGl().SamplerParameteri)
#undef glScissor
#define glScissor (::yw1::render::activeGl().Scissor)
#undef glShaderSource
#define glShaderSource (::yw1::render::activeGl().ShaderSource)
#undef glTexImage2D
#define glTexImage2D (::yw1::render::activeGl().TexImage2D)
#undef glTexParameteri
#define glTexParameteri (::yw1::render::activeGl().TexParameteri)
#undef glTexSubImage2D
#define glTexSubImage2D (::yw1::render::activeGl().TexSubImage2D)
#undef glUniform1i
#define glUniform1i (::yw1::render::activeGl().Uniform1i)
#undef glUniformMatrix4fv
#define glUniformMatrix4fv (::yw1::render::activeGl().UniformMatrix4fv)
#undef glUseProgram
#define glUseProgram (::yw1::render::activeGl().UseProgram)
#undef glVertexAttribPointer
#define glVertexAttribPointer (::yw1::render::activeGl().VertexAttribPointer)
#undef glViewport
#define glViewport (::yw1::render::activeGl().Viewport)
