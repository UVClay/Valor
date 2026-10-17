// OpenGL: 1.x functions come from libGL directly; newer 3.3 core functions are loaded through SDL.
#pragma once
#include <SDL3/SDL_opengl.h>

#define VALOR_GL_FUNCS(X)                                                                         \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers)                                               \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                                               \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D)                                     \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus)                                 \
    X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer)                                               \
    X(PFNGLGENBUFFERSPROC, glGenBuffers)                                                         \
    X(PFNGLBINDBUFFERPROC, glBindBuffer)                                                         \
    X(PFNGLBUFFERDATAPROC, glBufferData)                                                         \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                                               \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                                               \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)                               \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)                                       \
    X(PFNGLVERTEXATTRIBIPOINTERPROC, glVertexAttribIPointer)                                     \
    X(PFNGLCREATESHADERPROC, glCreateShader)                                                     \
    X(PFNGLSHADERSOURCEPROC, glShaderSource)                                                     \
    X(PFNGLCOMPILESHADERPROC, glCompileShader)                                                   \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv)                                                       \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                                             \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                                                   \
    X(PFNGLATTACHSHADERPROC, glAttachShader)                                                     \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram)                                                       \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                                                     \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                                           \
    X(PFNGLUSEPROGRAMPROC, glUseProgram)                                                         \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)                                         \
    X(PFNGLUNIFORM1IPROC, glUniform1i)                                                           \
    X(PFNGLUNIFORM2FPROC, glUniform2f)                                                           \
    X(PFNGLUNIFORM4IPROC, glUniform4i)                                                           \
    X(PFNGLBINDFRAGDATALOCATIONPROC, glBindFragDataLocation)                                     \
    X(PFNGLBLENDFUNCSEPARATEPROC, glBlendFuncSeparate)                                           \
    X(PFNGLBLENDEQUATIONSEPARATEPROC, glBlendEquationSeparate)

namespace psx::gl {

#define X(type, name) extern type name;
VALOR_GL_FUNCS(X)
#undef X

// Load all functions from the current context. Returns false if any is missing.
bool load();

}  // namespace psx::gl
