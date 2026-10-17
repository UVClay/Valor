// OpenGL renderer: replays GPU primitives into a framebuffer `scale` times PS1 VRAM size.
//
// The software rasterizer still runs and keeps the native VRAM exact (CPU readbacks, VRAM
// copies, render-to-texture). This renderer only produces the image you see. Textures are
// sampled from a copy of native VRAM, uploaded in 64x64 blocks when a draw samples a block the
// software rasterizer or the CPU changed. PS1 behavior kept: affine texturing (vertex w is 1),
// CLUT/texture window lookups, raw vs modulated textures, the four semi-transparency modes (two
// passes for textured primitives so only texels with the semi bit blend) and the mask bit.
#include <algorithm>
#include <cstring>
#include <vector>

#include "gl.h"
#include "gpu.h"
#include "gpu_gl.h"
#include "runtime.h"

using namespace psx::gl;

namespace psx::hw {

namespace {

bool enabled;
int S = 1;  // scale

GLuint target_tex, target_fbo;  // (1024*S) x (512*S) RGBA8; alpha = mask bit
GLuint temp_tex, temp_fbo;      // scratch for VRAM->VRAM copies
GLuint vram_tex;                // native VRAM, R16UI
GLuint program, vao, vbo;
GLint u_size, u_vram, u_texwin, u_scale, u_mask_set, u_quantize;

// 64x64 blocks of native VRAM that changed since they were last uploaded.
constexpr int BLOCK = 64, BX = 1024 / BLOCK, BY = 512 / BLOCK;
bool dirty[BY][BX];
bool any_dirty;

struct GLVertex {
    float x, y, u, v;
    uint8_t r, g, b, a;
    uint16_t tex_x, tex_y, clut_x, clut_y;
    uint16_t flags, pad;
};

// Vertex flags
constexpr uint16_t F_DEPTH = 3, F_TEXTURED = 4, F_RAW = 8, F_DITHER = 16;
constexpr uint16_t F_PASS_OPAQUE_TEXELS = 1 << 5, F_PASS_SEMI_TEXELS = 2 << 5;
constexpr uint16_t F_COPY = 128;  // copy native VRAM texels as-is (uploads)

// State shared by everything in the current batch.
struct BatchState {
    int blend = -1;  // -1 opaque, 0..3 semi-transparency mode
    int area_x1 = 0, area_y1 = 0, area_x2 = 1023, area_y2 = 511;
    bool scissor = true;
    uint32_t tex_window = 0;
    bool mask_set = false;
    bool operator==(const BatchState &) const = default;
} batch_state;

std::vector<GLVertex> batch;
bool warned_mask_check;

const char *VS = R"(#version 330 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
layout(location = 3) in uvec4 a_tex;
layout(location = 4) in uint a_flags;
uniform vec2 u_size;
out vec3 v_color;
noperspective out vec2 v_uv;
flat out uvec4 v_tex;
flat out uint v_flags;
void main() {
    v_color = a_color.rgb * 255.0;
    v_uv = a_uv;
    v_tex = a_tex;
    v_flags = a_flags;
    gl_Position = vec4(a_pos / u_size * 2.0 - 1.0, 0.0, 1.0);
}
)";

const char *FS = R"(#version 330 core
uniform usampler2D u_vram;
uniform ivec4 u_texwin;  // mask x, mask y, offset x, offset y (texels)
uniform int u_scale;
uniform int u_mask_set;
uniform int u_quantize;
in vec3 v_color;
noperspective in vec2 v_uv;
flat in uvec4 v_tex;
flat in uint v_flags;
out vec4 o_color;

const int DITHER[16] = int[16](-4, 0, -3, 1, 2, -2, 3, -1, -3, 1, -4, 0, 3, -1, 2, -2);

uint fetch(int x, int y) { return texelFetch(u_vram, ivec2(x & 1023, y & 511), 0).r; }

vec4 color15(uint t) {
    return vec4(vec3(t & 31u, (t >> 5) & 31u, (t >> 10) & 31u) / 31.0, (t & 0x8000u) != 0u ? 1.0 : 0.0);
}

void main() {
    if ((v_flags & 128u) != 0u) {  // upload copy
        uint t = fetch(int(floor(v_uv.x)), int(floor(v_uv.y)));
        o_color = color15(t);
        return;
    }
    vec3 c = v_color;
    bool mask = u_mask_set != 0;
    if ((v_flags & 4u) != 0u) {
        ivec2 uv = ivec2(floor(v_uv)) & 255;
        uv = (uv & ~u_texwin.xy) | (u_texwin.zw & u_texwin.xy);
        int tx = int(v_tex.x), ty = int(v_tex.y), cx = int(v_tex.z), cy = int(v_tex.w);
        uint depth = v_flags & 3u;
        uint t;
        if (depth == 0u) {
            uint w = fetch(tx + uv.x / 4, ty + uv.y);
            t = fetch(cx + int((w >> uint((uv.x & 3) * 4)) & 15u), cy);
        } else if (depth == 1u) {
            uint w = fetch(tx + uv.x / 2, ty + uv.y);
            t = fetch(cx + int((w >> uint((uv.x & 1) * 8)) & 255u), cy);
        } else {
            t = fetch(tx + uv.x, ty + uv.y);
        }
        if (t == 0u) discard;
        uint pass = (v_flags >> 5) & 3u;
        bool semi_texel = (t & 0x8000u) != 0u;
        if (pass == 1u && semi_texel) discard;
        if (pass == 2u && !semi_texel) discard;
        mask = mask || semi_texel;
        vec3 tc = vec3(t & 31u, (t >> 5) & 31u, (t >> 10) & 31u) * 8.0;
        c = (v_flags & 8u) != 0u ? tc : floor(tc * c / 128.0);
    }
    if ((v_flags & 16u) != 0u) {
        ivec2 p = ivec2(gl_FragCoord.xy) / u_scale;  // dither on the PS1's pixel grid
        c += float(DITHER[(p.y & 3) * 4 + (p.x & 3)]);
    }
    c = clamp(c, 0.0, 255.0);
    if (u_quantize != 0) c = floor(c / 8.0) * 8.0;
    o_color = vec4(c / 255.0, mask ? 1.0 : 0.0);
}
)";

GLuint compile(GLenum type, const char *src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char msg[2048];
        glGetShaderInfoLog(sh, sizeof msg, nullptr, msg);
        fatal("gl: shader compile failed: %s", msg);
    }
    return sh;
}

void make_target(GLuint &tex, GLuint &fbo) {
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1024 * S, 512 * S, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) fatal("gl: framebuffer incomplete");
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
}

void upload_dirty() {
    if (!any_dirty) return;
    glBindTexture(GL_TEXTURE_2D, vram_tex);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 1024);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    for (int by = 0; by < BY; by++)
        for (int bx = 0; bx < BX; bx++)
            if (dirty[by][bx]) {
                glTexSubImage2D(GL_TEXTURE_2D, 0, bx * BLOCK, by * BLOCK, BLOCK, BLOCK, GL_RED_INTEGER,
                                GL_UNSIGNED_SHORT, gpu.vram.data() + by * BLOCK * 1024 + bx * BLOCK);
                dirty[by][bx] = false;
            }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    any_dirty = false;
}

bool region_dirty(int x, int y, int w, int h) {
    if (!any_dirty) return false;
    for (int by = std::max(0, y) / BLOCK; by <= std::min(511, y + h - 1) / BLOCK; by++)
        for (int bx = std::max(0, x) / BLOCK; bx <= std::min(1023, x + w - 1) / BLOCK; bx++)
            if (dirty[by][bx]) return true;
    return false;
}

void apply_blend(int mode) {
    if (mode < 0) {
        glDisable(GL_BLEND);
        return;
    }
    glEnable(GL_BLEND);
    // RGB blends per the semi-transparency mode; alpha (mask bit) takes the new value.
    switch (mode) {
    case 0:  // B/2 + F/2
        glBlendColor(0, 0, 0, 0.5f);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        glBlendFuncSeparate(GL_CONSTANT_ALPHA, GL_CONSTANT_ALPHA, GL_ONE, GL_ZERO);
        break;
    case 1:  // B + F
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ZERO);
        break;
    case 2:  // B - F
        glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_ADD);
        glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ONE, GL_ZERO);
        break;
    case 3:  // B + F/4
        glBlendColor(0, 0, 0, 0.25f);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        glBlendFuncSeparate(GL_CONSTANT_ALPHA, GL_ONE, GL_ONE, GL_ZERO);
        break;
    }
}

void bind_target_for_drawing() {
    glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
    glViewport(0, 0, 1024 * S, 512 * S);
}

void flush() {
    if (batch.empty()) return;
    upload_dirty();
    bind_target_for_drawing();
    glUseProgram(program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, vram_tex);
    const BatchState &st = batch_state;
    uint32_t tw = st.tex_window;
    glUniform4i(u_texwin, int(tw & 0x1F) * 8, int((tw >> 5) & 0x1F) * 8, int((tw >> 10) & 0x1F) * 8,
                int((tw >> 15) & 0x1F) * 8);
    glUniform1i(u_mask_set, st.mask_set);
    if (st.scissor) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(st.area_x1 * S, st.area_y1 * S, (st.area_x2 - st.area_x1 + 1) * S, (st.area_y2 - st.area_y1 + 1) * S);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
    apply_blend(st.blend);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(batch.size() * sizeof(GLVertex)), batch.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(batch.size()));
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    batch.clear();
}

void set_state(int blend, bool scissor = true) {
    BatchState st;
    st.blend = blend;
    st.scissor = scissor;
    st.area_x1 = gpu.area_x1; st.area_y1 = gpu.area_y1;
    st.area_x2 = gpu.area_x2; st.area_y2 = gpu.area_y2;
    st.tex_window = gpu.tex_window;
    st.mask_set = gpu.mask_set;
    if (!(st == batch_state)) {
        flush();
        batch_state = st;
    }
    if (gpu.mask_check && !warned_mask_check) {
        warned_mask_check = true;
        log("gl: mask-bit test requested; not emulated by the OpenGL renderer yet");
    }
}

// Make sure the VRAM texture is current for the area a textured primitive samples.
void prepare_texture(const DrawState &s) {
    if (!s.textured) return;
    int w = s.tex_depth == 0 ? 64 : s.tex_depth == 1 ? 128 : 256;
    bool stale = region_dirty(s.tex_x, s.tex_y, w, 256);
    if (s.tex_depth < 2) stale = stale || region_dirty(s.clut_x, s.clut_y, s.tex_depth == 0 ? 16 : 256, 1);
    if (stale) {
        flush();
        upload_dirty();
    }
}

GLVertex make_vertex(const DrawState &s, float x, float y, float u, float v, int r, int g, int b, uint16_t flags) {
    GLVertex gv{};
    gv.x = x; gv.y = y; gv.u = u; gv.v = v;
    gv.r = uint8_t(r); gv.g = uint8_t(g); gv.b = uint8_t(b); gv.a = 255;
    gv.tex_x = uint16_t(s.tex_x); gv.tex_y = uint16_t(s.tex_y);
    gv.clut_x = uint16_t(s.clut_x); gv.clut_y = uint16_t(s.clut_y);
    gv.flags = flags;
    return gv;
}

uint16_t base_flags(const DrawState &s, bool dither) {
    uint16_t f = uint16_t(s.tex_depth & F_DEPTH);
    if (s.textured) f |= F_TEXTURED;
    if (s.raw) f |= F_RAW;
    if (dither && config.dither) f |= F_DITHER;
    return f;
}

// Emit a primitive (as triangles) once or twice depending on semi-transparency.
template <typename Emit>
void submit(const DrawState &s, Emit emit) {
    prepare_texture(s);
    if (!s.semi) {
        set_state(-1);
        emit(uint16_t(0));
    } else if (!s.textured) {
        set_state(s.semi_mode);
        emit(uint16_t(0));
    } else {
        set_state(-1);
        emit(F_PASS_OPAQUE_TEXELS);
        set_state(s.semi_mode);
        emit(F_PASS_SEMI_TEXELS);
    }
}

}  // namespace

namespace hwgl {

bool active() { return enabled; }

void triangle(const DrawState &s, const Vertex &a, const Vertex &b, const Vertex &c) {
    bool dither = s.dither && (s.shaded || (s.textured && !s.raw));
    submit(s, [&](uint16_t pass) {
        uint16_t f = base_flags(s, dither) | pass;
        for (const Vertex *v : {&a, &b, &c})
            batch.push_back(make_vertex(s, float(v->x), float(v->y), float(v->u), float(v->v), v->r, v->g, v->b, f));
    });
}

void line(const DrawState &s, const Vertex &a, const Vertex &b) {
    // A PS1 line lights one pixel per step along its major axis: draw a quad one native pixel
    // thick, from half a pixel before the first pixel center to half a pixel past the last.
    float ax = a.x + 0.5f, ay = a.y + 0.5f, bx = b.x + 0.5f, by = b.y + 0.5f;
    bool major_x = std::abs(b.x - a.x) >= std::abs(b.y - a.y);
    float ex = major_x ? (b.x >= a.x ? 0.5f : -0.5f) : 0, ey = major_x ? 0 : (b.y >= a.y ? 0.5f : -0.5f);
    float nx = major_x ? 0 : 0.5f, ny = major_x ? 0.5f : 0;
    bool dither = s.dither && s.shaded;
    submit(s, [&](uint16_t pass) {
        uint16_t f = base_flags(s, dither) | pass;
        GLVertex p0 = make_vertex(s, ax - ex - nx, ay - ey - ny, 0, 0, a.r, a.g, a.b, f);
        GLVertex p1 = make_vertex(s, ax - ex + nx, ay - ey + ny, 0, 0, a.r, a.g, a.b, f);
        GLVertex p2 = make_vertex(s, bx + ex - nx, by + ey - ny, 0, 0, b.r, b.g, b.b, f);
        GLVertex p3 = make_vertex(s, bx + ex + nx, by + ey + ny, 0, 0, b.r, b.g, b.b, f);
        for (const GLVertex *v : {&p0, &p1, &p2, &p1, &p3, &p2}) batch.push_back(*v);
    });
}

void rect(const DrawState &s, const Vertex &base, int w, int h) {
    float x0 = float(base.x), y0 = float(base.y), x1 = x0 + w, y1 = y0 + h;
    float u0 = float(base.u), v0 = float(base.v);
    submit(s, [&](uint16_t pass) {
        uint16_t f = base_flags(s, false) | pass;
        auto vx = [&](float x, float y, float u, float v) { return make_vertex(s, x, y, u, v, base.r, base.g, base.b, f); };
        GLVertex p0 = vx(x0, y0, u0, v0), p1 = vx(x1, y0, u0 + w, v0), p2 = vx(x0, y1, u0, v0 + h),
                 p3 = vx(x1, y1, u0 + w, v0 + h);
        for (const GLVertex *v : {&p0, &p1, &p2, &p1, &p3, &p2}) batch.push_back(*v);
    });
}

void fill(int x, int y, int w, int h, uint32_t color) {
    vram_changed(x, y, w, h);
    if (!enabled) return;
    flush();
    bind_target_for_drawing();
    glEnable(GL_SCISSOR_TEST);
    glScissor(x * S, y * S, std::min(w, 1024 - x) * S, std::min(h, 512 - y) * S);
    auto q = [&](int shift) { return float((color >> shift) & 0xF8) / 255.0f; };
    glClearColor(q(0), q(8), q(16), 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
}

void copy(int sx, int sy, int dx, int dy, int w, int h) {
    vram_changed(dx, dy, w, h);
    if (!enabled) return;
    flush();
    // Through a scratch target: a blit within one framebuffer may not overlap.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, target_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, temp_fbo);
    glBlitFramebuffer(sx * S, sy * S, (sx + w) * S, (sy + h) * S, 0, 0, w * S, h * S, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, temp_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target_fbo);
    glBlitFramebuffer(0, 0, w * S, h * S, dx * S, dy * S, (dx + w) * S, (dy + h) * S, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
}

void upload(int x, int y, int w, int h) {
    vram_changed(x, y, w, h);
    if (!enabled) return;
    // Draw the uploaded pixels into the high-resolution target as they are.
    set_state(-1, false);
    DrawState s{};
    float x0 = float(x), y0 = float(y), x1 = float(x + w), y1 = float(y + h);
    auto vx = [&](float px, float py) { return make_vertex(s, px, py, px, py, 0, 0, 0, F_COPY); };
    GLVertex p0 = vx(x0, y0), p1 = vx(x1, y0), p2 = vx(x0, y1), p3 = vx(x1, y1);
    for (const GLVertex *v : {&p0, &p1, &p2, &p1, &p3, &p2}) batch.push_back(*v);
    flush();
}

void vram_changed(int x, int y, int w, int h) {
    if (!enabled || w <= 0 || h <= 0) return;
    for (int by = std::max(0, y) / BLOCK; by <= std::min(511, y + h - 1) / BLOCK; by++)
        for (int bx = std::max(0, x) / BLOCK; bx <= std::min(1023, x + w - 1) / BLOCK; bx++) dirty[by][bx] = true;
    any_dirty = true;
}

}  // namespace hwgl

bool gl_renderer_init(int scale) {
    S = std::clamp(scale, 1, 16);
    make_target(target_tex, target_fbo);
    make_target(temp_tex, temp_fbo);

    glGenTextures(1, &vram_tex);
    glBindTexture(GL_TEXTURE_2D, vram_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, 1024, 512, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    program = glCreateProgram();
    glAttachShader(program, compile(GL_VERTEX_SHADER, VS));
    glAttachShader(program, compile(GL_FRAGMENT_SHADER, FS));
    glBindFragDataLocation(program, 0, "o_color");
    glLinkProgram(program);
    GLint ok;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) fatal("gl: program link failed");
    glUseProgram(program);
    u_size = glGetUniformLocation(program, "u_size");
    u_vram = glGetUniformLocation(program, "u_vram");
    u_texwin = glGetUniformLocation(program, "u_texwin");
    u_scale = glGetUniformLocation(program, "u_scale");
    u_mask_set = glGetUniformLocation(program, "u_mask_set");
    u_quantize = glGetUniformLocation(program, "u_quantize");
    glUniform1i(u_vram, 0);
    glUniform1i(u_scale, S);
    glUniform1i(u_quantize, config.dither ? 1 : 0);
    glUniform4i(u_texwin, 0, 0, 0, 0);
    glUniform2f(u_size, 1024.0f, 512.0f);

    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    auto off = [](size_t o) { return reinterpret_cast<const void *>(o); };
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GLVertex), off(offsetof(GLVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(GLVertex), off(offsetof(GLVertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GLVertex), off(offsetof(GLVertex, r)));
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 4, GL_UNSIGNED_SHORT, sizeof(GLVertex), off(offsetof(GLVertex, tex_x)));
    glEnableVertexAttribArray(4);
    glVertexAttribIPointer(4, 1, GL_UNSIGNED_SHORT, sizeof(GLVertex), off(offsetof(GLVertex, flags)));

    // Everything currently in VRAM must be uploaded before the first textured draw.
    for (auto &row : dirty) std::fill(std::begin(row), std::end(row), true);
    any_dirty = true;
    enabled = true;
    log("gl: renderer at %dx (%dx%d framebuffer)", S, 1024 * S, 512 * S);
    return true;
}

int gl_renderer_scale() { return S; }

std::vector<uint32_t> gl_renderer_read(int x, int y, int w, int h) {
    flush();
    std::vector<uint32_t> px(size_t(w) * S * h * S);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, target_fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(x * S, y * S, w * S, h * S, GL_BGRA, GL_UNSIGNED_BYTE, px.data());
    for (auto &p : px) p |= 0xFF000000u;
    return px;  // rows in VRAM order (top first)
}

// Copy the displayed area of the high-resolution framebuffer into the bound draw framebuffer.
void gl_renderer_present(int dst_x, int dst_y, int dst_w, int dst_h, int src_x, int src_y, int src_w, int src_h) {
    flush();
    glBindFramebuffer(GL_READ_FRAMEBUFFER, target_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    // The target stores VRAM row y at y; the window's origin is bottom-left, so flip.
    glBlitFramebuffer(src_x * S, src_y * S, (src_x + src_w) * S, (src_y + src_h) * S, dst_x, dst_y + dst_h, dst_x + dst_w,
                      dst_y, GL_COLOR_BUFFER_BIT, GL_LINEAR);
}

}  // namespace psx::hw
