// OpenGL renderer: replays GPU primitives into a framebuffer `scale` times PS1 VRAM size.
//
// The software rasterizer still runs and keeps the native VRAM exact (CPU readbacks, VRAM
// copies, render-to-texture). This renderer only produces the image you see. Textures are
// sampled from a copy of native VRAM, uploaded in 64x64 blocks when a draw samples a block the
// software rasterizer or the CPU changed. PS1 behavior kept: affine texturing (vertex w is 1),
// CLUT/texture window lookups, raw vs modulated textures, the four semi-transparency modes (two
// passes for textured primitives so only texels with the semi bit blend) and the mask bit.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <deque>
#include <memory>
#include <vector>

#include "gl.h"
#include "gpu.h"
#include "gpu_gl.h"
#include "interp.h"
#include "overlay.h"
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

// Frame interpolation (interp.cpp): everything drawn for one game tick, so it can be drawn again
// with the camera moved. Per vertex: the tick's projection it came from (rec), or for a vertex
// without one, the primitive's other vertices to borrow movement from (group).
struct VertexSource {
    int32_t rec = -1, group = -1;
};
std::vector<VertexSource> batch_sources;  // parallel to `batch`

struct FrameList {
    std::shared_ptr<const InterpTick> tick;
    std::vector<GLVertex> vertices;
    std::vector<VertexSource> sources;
    std::vector<std::array<int32_t, 4>> groups;  // projections of a primitive's vertices (-1 = none)
    struct Op {
        enum Kind { DRAW, FILL, COPY } kind;
        BatchState state;          // DRAW
        uint32_t first, count;     // DRAW: vertex range
        int x, y, w, h, sx, sy;    // FILL / COPY (sx, sy: copy source)
        uint32_t color;            // FILL
    };
    std::vector<Op> ops;
    bool has_area = false;
    int area_x1 = 0, area_y1 = 0, area_x2 = 0, area_y2 = 0;
    bool moved = false;            // the target holds an interpolated version of this list
    uint64_t shown_at = 0;         // vblank it was first displayed
    double period = 2;             // vblanks until the next tick is expected
    bool shown = false;
    uint64_t last_frame = 0;       // vblank of the latest draw
};
std::unique_ptr<FrameList> recording;            // the list being drawn now
std::deque<std::unique_ptr<FrameList>> finished;  // complete lists, newest last
FrameList *last_shown;

uint64_t source_stats[4];  // vertices: not projected, ambiguous, other projection, matched

// Widescreen HUD: while a HUD primitive is submitted, x is squeezed toward the frame's center.
struct {
    bool active;
    float center, factor;
} hud;

// The current primitive's projections (filled while submitting it).
std::array<int32_t, 4> prim_recs;
int prim_group = -1;

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
                overlay_counters.vram_uploads++;
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

void draw_vertices(const BatchState &st, const GLVertex *v, size_t n);

void flush() {
    if (batch.empty()) return;
    if (recording) {
        FrameList::Op op{};
        op.kind = FrameList::Op::DRAW;
        op.state = batch_state;
        op.first = uint32_t(recording->vertices.size());
        op.count = uint32_t(batch.size());
        recording->ops.push_back(op);
        recording->vertices.insert(recording->vertices.end(), batch.begin(), batch.end());
        recording->sources.insert(recording->sources.end(), batch_sources.begin(), batch_sources.end());
    }
    draw_vertices(batch_state, batch.data(), batch.size());
    batch.clear();
    batch_sources.clear();
}

void draw_vertices(const BatchState &st, const GLVertex *v, size_t n) {
    upload_dirty();
    bind_target_for_drawing();
    glUseProgram(program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, vram_tex);
    uint32_t tw = st.tex_window;
    glUniform4i(u_texwin, int(tw & 0x1F) * 8, int((tw >> 5) & 0x1F) * 8, int((tw >> 10) & 0x1F) * 8,
                int((tw >> 15) & 0x1F) * 8);
    glUniform1i(u_mask_set, st.mask_set);
    glUniform1i(u_quantize, config.dither ? 1 : 0);
    if (st.scissor) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(st.area_x1 * S, st.area_y1 * S, (st.area_x2 - st.area_x1 + 1) * S, (st.area_y2 - st.area_y1 + 1) * S);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
    apply_blend(st.blend);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(n * sizeof(GLVertex)), v, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(n));
    overlay_counters.draw_calls++;
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
}

// Frame interpolation bookkeeping at the start of every primitive or fill: a new tick's drawing
// starts a new list, as does drawing in a later vblank (2D screens project nothing).
void begin_draw() {
    if (!interp_enabled()) {
        recording.reset();
        return;
    }
    interp_note_draw();
    auto tick = interp_drawing_tick();
    if (recording && recording->tick == tick && recording->last_frame == gpu.frames) return;
    flush();
    if (recording) {
        finished.push_back(std::move(recording));
        while (finished.size() > 3) {
            if (last_shown == finished.front().get()) last_shown = nullptr;
            finished.pop_front();
        }
    }
    recording = std::make_unique<FrameList>();
    recording->tick = tick;
    recording->last_frame = gpu.frames;
}

// Find the projections of a primitive's vertices. Different points can land on the same screen
// coordinate; a primitive's vertices are projected together, so of several candidates take the
// one projected closest (in order) to the primitive's unambiguous vertices.
void vertex_sources(const Vertex *const *v, int n, int32_t *out) {
    for (int i = 0; i < 4; i++) prim_recs[i] = -1;
    if (!recording || !recording->tick) {
        for (int i = 0; i < n; i++) out[i] = -1;
        return;
    }
    const InterpTick &t = *recording->tick;
    int32_t first[4];
    int64_t anchor = 0;
    int anchors = 0;
    for (int i = 0; i < n; i++) {
        auto it = t.by_sxy.find(v[i]->key);
        first[i] = it == t.by_sxy.end() ? -1 : int32_t(it->second);
        if (first[i] >= 0 && t.previous[first[i]] < 0) {
            anchor += first[i];
            anchors++;
        }
    }
    for (int i = 0; i < n; i++) {
        int32_t pick = first[i];
        if (pick >= 0 && t.previous[pick] >= 0) {
            if (!anchors) {
                source_stats[1]++;
                out[i] = -1;
                continue;
            }
            int64_t center = anchor / anchors, best = INT64_MAX;
            for (int32_t c = first[i]; c >= 0; c = t.previous[c])
                if (std::abs(c - center) < best) {
                    best = std::abs(c - center);
                    pick = c;
                }
            if (best > 64) pick = -1;  // nothing projected near the rest of the primitive
        }
        if (pick < 0) {
            static const char *dump = getenv("VALOR_TRACE_INTERP_FRAME");
            if (dump && gpu.frames / 2 == strtoull(dump, nullptr, 0) / 2)
                log("interp: unmatched n=%d i=%d (%d,%d) key %08X", n, i, v[i]->x, v[i]->y, v[i]->key);
            source_stats[0]++;
            out[i] = -1;
        } else if (t.vertices[pick].h != float(config.world_h)) {
            source_stats[2]++;
            out[i] = -2;  // projected, but not with the world (weapon): stays put
        } else {
            source_stats[3]++;
            out[i] = prim_recs[i] = pick;
        }
    }
}

// After a primitive's vertices are looked up: a group for vertices without a projection.
void finish_sources(int n) {
    prim_group = -1;
    if (!recording) return;
    bool any = false;
    for (int i = 0; i < n; i++) any |= prim_recs[i] >= 0;
    if (!any) return;
    std::array<int32_t, 4> g = {-1, -1, -1, -1};
    for (int i = 0; i < n; i++) g[i] = prim_recs[i];
    prim_group = int32_t(recording->groups.size());
    recording->groups.push_back(g);
}

// Widescreen HUD: in a 3D frame, a primitive nobody projected and narrower than the screen is
// part of the 2D HUD (full-width ones are backdrops, fades and flashes). It's drawn at its 4:3
// proportions: squeezed toward the center, or with config.hud_edges, toward the left or right
// edge when it lies entirely in a top corner of the original frame (the compass and the ammo
// count; menus and messages elsewhere stay centered so their lines don't come apart). Primitives
// touching the frame's left or right edge stay on that edge.
void begin_hud(const int32_t *src, int n, int min_x, int max_x, int max_y) {
    hud.active = false;
    if (!config.widescreen_hud || gte_squeeze >= 1 || !recording || !recording->tick || !recording->has_area) return;
    if (recording->tick->frame + 3 < gpu.frames || recording->tick->vertices.size() < 64) return;  // 2D screen
    for (int i = 0; i < n; i++)
        if (src[i] != -1 || prim_recs[i] >= 0) return;
    int width = recording->area_x2 - recording->area_x1 + 1;
    if (max_x - min_x >= width * 9 / 10) return;
    hud.active = true;
    float left = float(recording->area_x1), right = float(recording->area_x2 + 1);
    hud.center = (left + right) * 0.5f;
    bool top = max_y <= recording->area_y1 + (recording->area_y2 - recording->area_y1 + 1) / 4;
    if (min_x <= left) hud.center = left;  // touching an edge (borders): stays on it
    else if (max_x >= right - 1) hud.center = right;
    else if (config.hud_edges && top && max_x <= left + width / 4) hud.center = left;
    else if (config.hud_edges && top && min_x >= right - width / 4) hud.center = right;
    hud.factor = float(gte_squeeze);
}

VertexSource source_for(int32_t rec) {
    VertexSource s;
    if (rec >= 0) s.rec = rec;
    else if (rec == -1) s.group = prim_group;
    return s;
}

void note_area() {
    if (!recording || recording->has_area) return;
    recording->has_area = true;
    recording->area_x1 = gpu.area_x1; recording->area_y1 = gpu.area_y1;
    recording->area_x2 = gpu.area_x2; recording->area_y2 = gpu.area_y2;
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
    if (hud.active) x = hud.center + (x - hud.center) * hud.factor;
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

void fill_target(int x, int y, int w, int h, uint32_t color);
void copy_target(int sx, int sy, int dx, int dy, int w, int h);

}  // namespace

namespace hwgl {

bool active() { return enabled; }

void triangle(const DrawState &s, const Vertex &a, const Vertex &b, const Vertex &c) {
    begin_draw();
    note_area();
    int32_t src[3];
    const Vertex *vs[3] = {&a, &b, &c};
    vertex_sources(vs, 3, src);
    finish_sources(3);
    begin_hud(src, 3, std::min({a.x, b.x, c.x}), std::max({a.x, b.x, c.x}), std::max({a.y, b.y, c.y}));
    bool dither = s.dither && (s.shaded || (s.textured && !s.raw));
    submit(s, [&](uint16_t pass) {
        uint16_t f = base_flags(s, dither) | pass;
        int i = 0;
        for (const Vertex *v : {&a, &b, &c}) {
            batch.push_back(make_vertex(s, float(v->x), float(v->y), float(v->u), float(v->v), v->r, v->g, v->b, f));
            batch_sources.push_back(source_for(src[i++]));
        }
    });
    hud.active = false;
}

void line(const DrawState &s, const Vertex &a, const Vertex &b) {
    // A PS1 line lights one pixel per step along its major axis: draw a quad one native pixel
    // thick, from half a pixel before the first pixel center to half a pixel past the last.
    float ax = a.x + 0.5f, ay = a.y + 0.5f, bx = b.x + 0.5f, by = b.y + 0.5f;
    bool major_x = std::abs(b.x - a.x) >= std::abs(b.y - a.y);
    float ex = major_x ? (b.x >= a.x ? 0.5f : -0.5f) : 0, ey = major_x ? 0 : (b.y >= a.y ? 0.5f : -0.5f);
    float nx = major_x ? 0 : 0.5f, ny = major_x ? 0.5f : 0;
    begin_draw();
    note_area();
    int32_t src[2];
    const Vertex *vs[2] = {&a, &b};
    vertex_sources(vs, 2, src);
    finish_sources(2);
    begin_hud(src, 2, std::min(a.x, b.x), std::max(a.x, b.x), std::max(a.y, b.y));
    bool dither = s.dither && s.shaded;
    submit(s, [&](uint16_t pass) {
        uint16_t f = base_flags(s, dither) | pass;
        for (int i : {0, 0, 1, 0, 1, 1}) batch_sources.push_back(source_for(src[i]));
        GLVertex p0 = make_vertex(s, ax - ex - nx, ay - ey - ny, 0, 0, a.r, a.g, a.b, f);
        GLVertex p1 = make_vertex(s, ax - ex + nx, ay - ey + ny, 0, 0, a.r, a.g, a.b, f);
        GLVertex p2 = make_vertex(s, bx + ex - nx, by + ey - ny, 0, 0, b.r, b.g, b.b, f);
        GLVertex p3 = make_vertex(s, bx + ex + nx, by + ey + ny, 0, 0, b.r, b.g, b.b, f);
        for (const GLVertex *v : {&p0, &p1, &p2, &p1, &p3, &p2}) batch.push_back(*v);
    });
    hud.active = false;
}

void rect(const DrawState &s, const Vertex &base, int w, int h) {
    float x0 = float(base.x), y0 = float(base.y), x1 = x0 + w, y1 = y0 + h;
    float u0 = float(base.u), v0 = float(base.v);
    begin_draw();
    note_area();
    int32_t src;
    const Vertex *vs[1] = {&base};
    vertex_sources(vs, 1, &src);
    finish_sources(1);
    begin_hud(&src, 1, base.x, base.x + w, base.y + h);
    submit(s, [&](uint16_t pass) {
        uint16_t f = base_flags(s, false) | pass;
        for (int i = 0; i < 6; i++) batch_sources.push_back(source_for(src));
        auto vx = [&](float x, float y, float u, float v) { return make_vertex(s, x, y, u, v, base.r, base.g, base.b, f); };
        GLVertex p0 = vx(x0, y0, u0, v0), p1 = vx(x1, y0, u0 + w, v0), p2 = vx(x0, y1, u0, v0 + h),
                 p3 = vx(x1, y1, u0 + w, v0 + h);
        for (const GLVertex *v : {&p0, &p1, &p2, &p1, &p3, &p2}) batch.push_back(*v);
    });
    hud.active = false;
}

void fill(int x, int y, int w, int h, uint32_t color) {
    vram_changed(x, y, w, h);
    if (!enabled) return;
    begin_draw();
    flush();
    if (recording) {
        FrameList::Op op{};
        op.kind = FrameList::Op::FILL;
        op.x = x; op.y = y; op.w = w; op.h = h; op.color = color;
        recording->ops.push_back(op);
    }
    fill_target(x, y, w, h, color);
}

}  // namespace hwgl

namespace {

void fill_target(int x, int y, int w, int h, uint32_t color) {
    bind_target_for_drawing();
    glEnable(GL_SCISSOR_TEST);
    glScissor(x * S, y * S, std::min(w, 1024 - x) * S, std::min(h, 512 - y) * S);
    auto q = [&](int shift) { return float((color >> shift) & 0xF8) / 255.0f; };
    glClearColor(q(0), q(8), q(16), 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
}

}  // namespace

namespace hwgl {

void copy(int sx, int sy, int dx, int dy, int w, int h) {
    vram_changed(dx, dy, w, h);
    if (!enabled) return;
    flush();
    if (recording) {
        FrameList::Op op{};
        op.kind = FrameList::Op::COPY;
        op.sx = sx; op.sy = sy; op.x = dx; op.y = dy; op.w = w; op.h = h;
        recording->ops.push_back(op);
    }
    copy_target(sx, sy, dx, dy, w, h);
}

}  // namespace hwgl

namespace {

void copy_target(int sx, int sy, int dx, int dy, int w, int h) {
    // Through a scratch target: a blit within one framebuffer may not overlap.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, target_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, temp_fbo);
    glBlitFramebuffer(sx * S, sy * S, (sx + w) * S, (sy + h) * S, 0, 0, w * S, h * S, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, temp_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target_fbo);
    glBlitFramebuffer(0, 0, w * S, h * S, dx * S, dy * S, (dx + w) * S, (dy + h) * S, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
}

}  // namespace

namespace hwgl {

void upload(int x, int y, int w, int h) {
    vram_changed(x, y, w, h);
    if (!enabled) return;
    // Draw the uploaded pixels into the high-resolution target as they are.
    set_state(-1, false);
    DrawState s{};
    float x0 = float(x), y0 = float(y), x1 = float(x + w), y1 = float(y + h);
    auto vx = [&](float px, float py) { return make_vertex(s, px, py, px, py, 0, 0, 0, F_COPY); };
    GLVertex p0 = vx(x0, y0), p1 = vx(x1, y0), p2 = vx(x0, y1), p3 = vx(x1, y1);
    for (const GLVertex *v : {&p0, &p1, &p2, &p1, &p3, &p2}) {
        batch.push_back(*v);
        batch_sources.push_back({});
    }
    flush();
}

void vram_changed(int x, int y, int w, int h) {
    if (!enabled || w <= 0 || h <= 0) return;
    for (int by = std::max(0, y) / BLOCK; by <= std::min(511, y + h - 1) / BLOCK; by++)
        for (int bx = std::max(0, x) / BLOCK; bx <= std::min(1023, x + w - 1) / BLOCK; bx++) dirty[by][bx] = true;
    any_dirty = true;
}

}  // namespace hwgl

static void make_targets() {
    make_target(target_tex, target_fbo);
    make_target(temp_tex, temp_fbo);
}

static void free_targets() {
    GLuint tex[2] = {target_tex, temp_tex}, fbo[2] = {target_fbo, temp_fbo};
    glDeleteTextures(2, tex);
    glDeleteFramebuffers(2, fbo);
}

// Repaint the whole high-resolution target from native VRAM (after creating or resizing it).
static void repaint_from_vram() {
    for (auto &row : dirty) std::fill(std::begin(row), std::end(row), true);
    any_dirty = true;
    hwgl::upload(0, 0, 1024, 512);
}

void gl_renderer_set_scale(int scale) {
    scale = std::clamp(scale, 1, 16);
    if (!program) {  // never initialized
        if (scale > 1) gl_renderer_init(scale);
        return;
    }
    flush();
    if (scale == 1) {
        enabled = false;
        log("gl: software renderer");
        return;
    }
    if (scale != S || !enabled) {
        free_targets();
        S = scale;
        make_targets();
        glUseProgram(program);
        glUniform1i(u_scale, S);
        enabled = true;
        repaint_from_vram();
        log("gl: renderer at %dx (%dx%d framebuffer)", S, 1024 * S, 512 * S);
    }
}

bool gl_renderer_init(int scale) {
    S = std::clamp(scale, 1, 16);
    make_targets();

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

int gl_renderer_scale() { return enabled ? S : 1; }

std::vector<uint32_t> gl_renderer_read(int x, int y, int w, int h) {
    flush();
    std::vector<uint32_t> px(size_t(w) * S * h * S);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, target_fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(x * S, y * S, w * S, h * S, GL_BGRA, GL_UNSIGNED_BYTE, px.data());
    for (auto &p : px) p |= 0xFF000000u;
    return px;  // rows in VRAM order (top first)
}

// Frame interpolation: redraw the displayed tick's list with the camera `vblank_time` along
// toward the next tick's (vblank_time: emulated vblanks, fractional).
void gl_renderer_interpolate(double vblank_time) {
    if (!enabled || !interp_presenting()) return;
    if (recording && (recording->tick != interp_drawing_tick() || recording->last_frame < gpu.frames)) {
        // A new tick's projections started (or a vblank passed): the list is complete.
        flush();
        finished.push_back(std::move(recording));
        while (finished.size() > 3) {
            if (last_shown == finished.front().get()) last_shown = nullptr;
            finished.pop_front();
        }
    }
    // The newest complete list drawn where the display shows.
    FrameList *list = nullptr;
    for (auto it = finished.rbegin(); it != finished.rend() && !list; ++it) {
        FrameList *l = it->get();
        if (l->has_area && gpu.display_x >= l->area_x1 && gpu.display_x <= l->area_x2 &&
            gpu.display_y >= l->area_y1 && gpu.display_y <= l->area_y2)
            list = l;
    }
    if (!list || !list->tick) return;
    uint64_t now = uint64_t(vblank_time);
    if (!list->shown) {
        list->shown = true;
        list->shown_at = now;
        if (last_shown) list->period = std::clamp(double(now - last_shown->shown_at), 1.0, 4.0);
        last_shown = list;
    }
    double alpha = std::clamp((vblank_time - double(list->shown_at)) / list->period, 0.0, 1.0);
    auto next = interp_tick(list->tick->seq + 1);
    static InterpMotion motion;
    bool ok = next && alpha > 0 && interp_motion(*list->tick, *next, alpha, motion);
    if (!ok && !list->moved) return;  // the target already shows the list as drawn
    list->moved = ok;
    if (static bool trace = getenv("VALOR_TRACE_INTERP"); trace) {
        size_t rec = 0, group = 0;
        for (const auto &s : list->sources) { rec += s.rec >= 0; group += s.group >= 0; }
        log("interp: sources none %llu ambiguous %llu other %llu matched %llu", (unsigned long long)source_stats[0],
            (unsigned long long)source_stats[1], (unsigned long long)source_stats[2], (unsigned long long)source_stats[3]);
        memset(source_stats, 0, sizeof source_stats);
        log("interp: t=%.2f tick %llu alpha %.2f ok %d: %zu vertices, %zu matched, %zu by group, %zu projections, "
            "%zu transforms, %zu moving",
            vblank_time, (unsigned long long)list->tick->seq, alpha, ok, list->sources.size(), rec, group,
            list->tick->vertices.size(), list->tick->groups.size(), ok ? motion.objects.size() : 0);
    }

    // Screen movement of every projection this tick.
    const auto &pv = list->tick->vertices;
    static std::vector<float> dx, dy;
    static std::vector<uint8_t> valid;
    dx.assign(pv.size(), 0);
    dy.assign(pv.size(), 0);
    valid.assign(pv.size(), 0);
    for (size_t i = 0; i < pv.size(); i++) {
        const InterpVertex &p = pv[i];
        if (!ok || p.h != float(config.world_h) || !p.exact) continue;
        double q[3];
        int32_t object = p.group < int32_t(motion.group_object.size()) ? motion.group_object[p.group] : -1;
        if (object >= 0) {
            const auto &o = motion.objects[object];
            for (int r = 0; r < 3; r++) q[r] = o.m[r][0] * p.v[0] + o.m[r][1] * p.v[1] + o.m[r][2] * p.v[2] + o.t[r];
        } else {
            for (int r = 0; r < 3; r++)
                q[r] = motion.a[r][0] * p.x + motion.a[r][1] * p.y + motion.a[r][2] * p.z + (p.translated ? motion.b[r] : 0);
        }
        if (q[2] < p.h * 0.5) continue;
        dx[i] = float(p.h * (q[0] / q[2] - p.x / p.z) * p.kx);
        dy[i] = float(p.h * (q[1] / q[2] - p.y / p.z) * p.ky);
        valid[i] = 1;
    }
    static std::vector<GLVertex> moved;
    moved = list->vertices;
    for (size_t i = 0; i < moved.size(); i++) {
        const VertexSource &s = list->sources[i];
        float mx = 0, my = 0;
        if (s.rec >= 0 && valid[s.rec]) {
            mx = dx[s.rec];
            my = dy[s.rec];
        } else if (s.group >= 0 || s.rec >= 0) {
            // No usable projection: move with the primitive's other vertices.
            int n = 0;
            if (s.group >= 0)
                for (int32_t r : list->groups[s.group])
                    if (r >= 0 && valid[r]) { mx += dx[r]; my += dy[r]; n++; }
            if (n) { mx /= float(n); my /= float(n); }
        }
        moved[i].x += mx;
        moved[i].y += my;
    }
    flush();
    for (const auto &op : list->ops) {
        switch (op.kind) {
        case FrameList::Op::DRAW: draw_vertices(op.state, moved.data() + op.first, op.count); break;
        case FrameList::Op::FILL: fill_target(op.x, op.y, op.w, op.h, op.color); break;
        case FrameList::Op::COPY: copy_target(op.sx, op.sy, op.x, op.y, op.w, op.h); break;
        }
    }
}

// Copy the displayed area of the high-resolution framebuffer into the bound draw framebuffer.
void gl_renderer_present(int dst_x, int dst_y, int dst_w, int dst_h, int src_x, int src_y, int src_w, int src_h) {
    flush();
    glBindFramebuffer(GL_READ_FRAMEBUFFER, target_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    // The target stores VRAM row y at y; the window's origin is bottom-left, so flip.
    glBlitFramebuffer(src_x * S, src_y * S, (src_x + src_w) * S, (src_y + src_h) * S, dst_x, dst_y + dst_h, dst_x + dst_w,
                      dst_y, GL_COLOR_BUFFER_BIT, config.smooth_scaling ? GL_LINEAR : GL_NEAREST);
}

}  // namespace psx::hw
