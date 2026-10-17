// OpenGL function loading.
#include <SDL3/SDL.h>

#include "gl.h"
#include "runtime.h"

namespace psx::gl {

#define X(type, name) type name;
VALOR_GL_FUNCS(X)
#undef X

bool load() {
    bool ok = true;
#define X(type, name)                                                      \
    name = reinterpret_cast<type>(SDL_GL_GetProcAddress(#name));           \
    if (!name) {                                                           \
        log("gl: missing %s", #name);                                      \
        ok = false;                                                        \
    }
    VALOR_GL_FUNCS(X)
#undef X
    return ok;
}

}  // namespace psx::gl
