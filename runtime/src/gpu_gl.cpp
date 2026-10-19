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
#include <climits>
#include <cmath>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "game.h"
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
GLuint target_depth;            // depth renderbuffer of the target (config.depth_buffer)
GLuint vram_tex;                // native VRAM, R16UI
GLuint program, vao, vbo;
GLint u_size, u_vram, u_texwin, u_scale, u_mask_set, u_quantize, u_filter;

// 64x64 blocks of native VRAM that changed since they were last uploaded.
constexpr int BLOCK = 64, BX = 1024 / BLOCK, BY = 512 / BLOCK;
bool dirty[BY][BX];
bool any_dirty;

struct GLVertex {
    float x, y, u, v;
    uint8_t r, g, b, a;
    uint16_t tex_x, tex_y, clut_x, clut_y;
    uint16_t flags, pad;
    float depth;  // depth buffer (config.depth_buffer): 0 nearest .. 1
    float w;      // camera depth, for perspective-correct texturing (near-clipped polygons), else 1
};

// Vertex flags
constexpr uint16_t F_DEPTH = 3, F_TEXTURED = 4, F_RAW = 8, F_DITHER = 16;
constexpr uint16_t F_PASS_OPAQUE_TEXELS = 1 << 5, F_PASS_SEMI_TEXELS = 2 << 5;
constexpr uint16_t F_COPY = 128;  // copy native VRAM texels as-is (uploads)
constexpr uint16_t F_FILTER = 256;  // 3D: filtered with config.texture_filter

// State shared by everything in the current batch.
struct BatchState {
    int blend = -1;  // -1 opaque, 0..3 semi-transparency mode
    int area_x1 = 0, area_y1 = 0, area_x2 = 1023, area_y2 = 511;
    bool scissor = true;
    uint32_t tex_window = 0;
    bool mask_set = false;
    uint8_t depth = 0;  // 0: painter's order only, 1: depth test and write, 2: test only
    bool operator==(const BatchState &) const = default;
} batch_state;

std::vector<GLVertex> batch;
bool warned_mask_check;

// Frame interpolation (interp.cpp): everything drawn for one game tick, so it can be drawn again
// with the camera moved. Per vertex: the tick's projection it came from (rec), or for a vertex
// without one, the primitive's other vertices to borrow movement from (group).
struct VertexSource {
    int32_t rec = -1, group = -1;
    int32_t near = -1;  // a vertex of a near-clipped triangle: FrameList::near, and which one
    int8_t near_slot = 0;
    float depth_bias = -1;  // depth-tested (config.depth_buffer): its primitive's bias, else -1
    uint32_t key = 0;  // screen position as drawn (for vertices without a projection)
    bool hud = false;  // part of the 2D HUD: never moved
    bool world = false;  // drawn with the world's camera: turns with mouse look
};
std::vector<VertexSource> batch_sources;  // parallel to `batch`

// A triangle with a corner closer than the GTE projects properly (near_triangle): its corners in
// camera space with their texture coordinates and colors, the drawing offset, and corrections
// keeping corners the GTE projected properly at its (integer) position.
struct NearTri {
    InterpVertex p[3];
    bool perspective;  // textured perspective-correct (a corner was too close for the GTE)
    float u[3], v[3], r[3], g[3], b[3];
    float cx[3], cy[3];
    float ox, oy;
};
// It clipped at the near plane and projected: up to two triangles (unused corners collapse onto
// the first). `z` is the camera depth.
struct NearCorner { float x, y, u, v, r, g, b, z; };
constexpr int NEAR_CORNERS = 6;

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
    uint32_t world_prims = 0;      // primitives with world vertices
    // Triangles clipped at the near plane (see near_triangle), from their camera-space corners.
    std::vector<struct NearTri> near;
};
std::unique_ptr<FrameList> recording;            // the list being drawn now
std::deque<std::unique_ptr<FrameList>> finished;  // complete lists, newest last
FrameList *last_shown;

uint64_t source_stats[4];  // vertices: not projected, ambiguous, other projection, matched

// Widescreen HUD: while a HUD primitive is submitted, x is squeezed toward the frame's center.
struct {
    bool is_hud;  // the primitive being submitted is part of the HUD
    bool active;  // ... and is squeezed
    bool world;   // the primitive being submitted is part of the 3D world (not the HUD, weapon, backdrops)
    float center, factor;
} hud;

// Depth buffer (config.depth_buffer). The game sorts polygons by their average depth, so a large
// wall can sort behind something that's really behind it, which then shows through. World
// triangles are depth-tested by their vertices' real depth instead; a tiny bias per primitive
// keeps the game's order where depths tie (decals on walls). The sky, the weapon, the HUD and
// anything not projected keep the game's order.
uint8_t prim_depth_mode;  // the primitive being submitted: BatchState::depth
float prim_depth[4], prim_bias;
uint32_t depth_seq;  // primitives depth-tested in this list

float depth_of(float z) { return std::max(0.0f, 1.0f - 16.0f / std::max(z, 16.0f)); }

// Before submitting a world primitive whose vertices are at camera depths z.
void begin_depth(const float *z, int n, bool semi) {
    prim_depth_mode = 0;
    if (!config.depth_buffer) return;
    prim_bias = float(++depth_seq) * 2e-7f;
    for (int i = 0; i < n; i++) prim_depth[i] = depth_of(z[i]) - prim_bias;
    prim_depth_mode = semi ? 2 : 1;
}

// The current primitive's projections (filled while submitting it): world ones, and any.
std::array<int32_t, 4> prim_recs, prim_any;
// Sub-pixel corrections of this list's points, by packet coordinates (see precise_positions).
std::unordered_map<uint32_t, std::pair<float, float>> subpixel_points;
int prim_group = -1;
bool prim_other = false;  // the current primitive has vertices projected not with the world (the weapon)

const char *VS = R"(#version 330 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_color;
layout(location = 3) in uvec4 a_tex;
layout(location = 4) in uint a_flags;
layout(location = 5) in float a_depth;
layout(location = 6) in float a_w;
uniform vec2 u_size;
out vec3 v_color;
noperspective out vec3 v_uvq;  // (u, v, 1) / w: linear on screen
flat out uvec4 v_tex;
flat out uint v_flags;
void main() {
    v_color = a_color.rgb * 255.0;
    v_uvq = vec3(a_uv, 1.0) / a_w;
    v_tex = a_tex;
    v_flags = a_flags;
    gl_Position = vec4(a_pos / u_size * 2.0 - 1.0, a_depth * 2.0 - 1.0, 1.0);
}
)";

const char *FS = R"(#version 330 core
uniform usampler2D u_vram;
uniform ivec4 u_texwin;  // mask x, mask y, offset x, offset y (texels)
uniform int u_scale;
uniform int u_mask_set;
uniform int u_quantize;
uniform int u_filter;  // bilinear texture filtering (for 3D: flag 256)
in vec3 v_color;
noperspective in vec3 v_uvq;
flat in uvec4 v_tex;
flat in uint v_flags;
out vec4 o_color;

const int DITHER[16] = int[16](-4, 0, -3, 1, 2, -2, 3, -1, -3, 1, -4, 0, 3, -1, 2, -2);

uint fetch(int x, int y) { return texelFetch(u_vram, ivec2(x & 1023, y & 511), 0).r; }

vec4 color15(uint t) {
    return vec4(vec3(t & 31u, (t >> 5) & 31u, (t >> 10) & 31u) / 31.0, (t & 0x8000u) != 0u ? 1.0 : 0.0);
}

// A texel of the primitive's texture (page, CLUT, texture window), as a 15-bit color.
uint texel(ivec2 uv) {
    uv &= 255;
    uv = (uv & ~u_texwin.xy) | (u_texwin.zw & u_texwin.xy);
    int tx = int(v_tex.x), ty = int(v_tex.y), cx = int(v_tex.z), cy = int(v_tex.w);
    uint depth = v_flags & 3u;
    if (depth == 0u) {
        uint w = fetch(tx + uv.x / 4, ty + uv.y);
        return fetch(cx + int((w >> uint((uv.x & 3) * 4)) & 15u), cy);
    } else if (depth == 1u) {
        uint w = fetch(tx + uv.x / 2, ty + uv.y);
        return fetch(cx + int((w >> uint((uv.x & 1) * 8)) & 255u), cy);
    }
    return fetch(tx + uv.x, ty + uv.y);
}

void main() {
    vec2 v_uv = v_uvq.xy / v_uvq.z;
    if ((v_flags & 128u) != 0u) {  // upload copy
        uint t = fetch(int(floor(v_uv.x)), int(floor(v_uv.y)));
        o_color = color15(t);
        return;
    }
    vec3 c = v_color;
    bool mask = u_mask_set != 0;
    if ((v_flags & 4u) != 0u) {
        vec3 tc;
        bool semi_texel;
        if (u_filter == 0 || (v_flags & 256u) == 0u) {
            uint t = texel(ivec2(floor(v_uv)));
            if (t == 0u) discard;
            semi_texel = (t & 0x8000u) != 0u;
            tc = vec3(t & 31u, (t >> 5) & 31u, (t >> 10) & 31u) * 8.0;
        } else {
            // Bilinear: transparent texels (0) carry no weight, and the blend is drawn where most
            // of the weight is opaque.
            vec2 f = v_uv - 0.5;
            ivec2 i = ivec2(floor(f));
            vec2 w = f - floor(f);
            uint ts[4] = uint[4](texel(i), texel(i + ivec2(1, 0)), texel(i + ivec2(0, 1)), texel(i + ivec2(1, 1)));
            float ws[4] = float[4]((1.0 - w.x) * (1.0 - w.y), w.x * (1.0 - w.y), (1.0 - w.x) * w.y, w.x * w.y);
            vec3 sum = vec3(0.0);
            float total = 0.0, semi = 0.0;
            for (int k = 0; k < 4; k++) {
                if (ts[k] == 0u) continue;
                sum += ws[k] * vec3(ts[k] & 31u, (ts[k] >> 5) & 31u, (ts[k] >> 10) & 31u);
                total += ws[k];
                if ((ts[k] & 0x8000u) != 0u) semi += ws[k];
            }
            if (total < 0.5) discard;
            tc = sum / total * 8.0;
            semi_texel = semi > total * 0.5;
        }
        uint pass = (v_flags >> 5) & 3u;
        if (pass == 1u && semi_texel) discard;
        if (pass == 2u && !semi_texel) discard;
        mask = mask || semi_texel;
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

void make_target(GLuint &tex, GLuint &fbo, GLuint *depth = nullptr) {
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1024 * S, 512 * S, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (depth) {
        glGenRenderbuffers(1, depth);
        glBindRenderbuffer(GL_RENDERBUFFER, *depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 1024 * S, 512 * S);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, *depth);
    }
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) fatal("gl: framebuffer incomplete");
    glClearColor(0, 0, 0, 0);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | (depth ? GL_DEPTH_BUFFER_BIT : 0));
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
    glUniform1i(u_filter, config.texture_filter ? 1 : 0);
    if (st.scissor) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(st.area_x1 * S, st.area_y1 * S, (st.area_x2 - st.area_x1 + 1) * S, (st.area_y2 - st.area_y1 + 1) * S);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
    apply_blend(st.blend);
    if (st.depth) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(st.depth == 1 ? GL_TRUE : GL_FALSE);
    }
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(n * sizeof(GLVertex)), v, GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(n));
    overlay_counters.draw_calls++;
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
}

// Frame interpolation bookkeeping at the start of every primitive or fill: a new tick's drawing
// starts a new list, as does drawing in a later vblank (2D screens project nothing).
void layout_hud(const FrameList &list);

void bind_target_for_drawing();

// A new frame's drawing starts with an empty depth buffer.
void clear_depth() {
    depth_seq = 0;
    if (!config.depth_buffer) return;
    flush();
    bind_target_for_drawing();
    glDepthMask(GL_TRUE);
    glClearDepth(1.0);
    glClear(GL_DEPTH_BUFFER_BIT);
}

// The list being recorded is complete.
void finish_list() {
    flush();
    if (!recording) return;
    layout_hud(*recording);
    static const char *trace = getenv("VALOR_TRACE_LISTS");
    if (trace && recording->tick)
        log("list: frame %llu tick frame %llu seq %llu verts %zu world_prims %u ops %zu", (unsigned long long)gpu.frames,
            (unsigned long long)recording->tick->frame, (unsigned long long)recording->tick->seq,
            recording->tick->vertices.size(), recording->world_prims, recording->ops.size());
    finished.push_back(std::move(recording));
    while (finished.size() > 3) {
        if (last_shown == finished.front().get()) last_shown = nullptr;
        finished.pop_front();
    }
}

void begin_draw() {
    if (!interp_enabled()) {
        recording.reset();
        return;
    }
    interp_note_draw();
    auto tick = interp_drawing_tick();
    if (recording && recording->tick == tick && recording->last_frame == gpu.frames) return;
    finish_list();
    subpixel_points.clear();
    clear_depth();
    recording = std::make_unique<FrameList>();
    recording->tick = tick;
    recording->last_frame = gpu.frames;
}

// Find the projections of a primitive's vertices. Different points can land on the same screen
// coordinate; a primitive's vertices are projected together, so of several candidates take the
// one projected closest (in order) to the primitive's unambiguous vertices.
void vertex_sources(const Vertex *const *v, int n, int32_t *out) {
    for (int i = 0; i < 4; i++) prim_recs[i] = prim_any[i] = -1;
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
            prim_any[i] = pick;
            source_stats[2]++;
            out[i] = -2;  // projected, but not with the world (weapon): stays put
        } else {
            source_stats[3]++;
            out[i] = prim_recs[i] = prim_any[i] = pick;
        }
    }
}

// After a primitive's vertices are looked up: a group for vertices without a projection.
void finish_sources(int n) {
    prim_group = -1;
    prim_other = false;
    for (int i = 0; i < n; i++) prim_other |= prim_any[i] >= 0 && prim_recs[i] < 0;
    if (!recording) return;
    bool any = false;
    for (int i = 0; i < n; i++) any |= prim_recs[i] >= 0;
    if (!any) return;
    std::array<int32_t, 4> g = {-1, -1, -1, -1};
    for (int i = 0; i < n; i++) g[i] = prim_recs[i];
    prim_group = int32_t(recording->groups.size());
    recording->groups.push_back(g);
}

// Widescreen HUD. In a 3D frame, the HUD is the primitives nobody projected that aren't
// full-width (backdrops, fades and flashes): sprites and lines, and polygons from outside the
// game's primitive pool (where the previous frame's packets with world vertices were; world
// polygons clipped by the CPU come from there too).
// They're drawn at their 4:3 proportions, squeezed toward the center. With config.hud_edges, HUD
// elements entirely in a top corner of the original frame, close to its side (the compass, the
// ammo count), are squeezed toward that edge instead. An element is a group of primitives that
// touch on screen (glyphs of a line, segments of the compass), so its parts stay together (menus
// and messages stay centered). A frame's primitives take the anchor of the previous frame's
// element they're in (the HUD hardly moves; packet addresses do, as the world's primitive count
// changes), or else the corner rule for themselves alone.
// Primitives touching the frame's left or right edge stay on that edge.
struct HudPrim { uint32_t packet; int min_x, max_x, min_y, max_y; };
std::vector<HudPrim> hud_prims;  // this frame's
// The latest frame's elements, relative to its drawing area, and their anchor (-1 left, 0 center,
// 1 right).
struct HudBox { int min_x, max_x, min_y, max_y; int8_t anchor; };
std::vector<HudBox> hud_boxes;
std::vector<uint32_t> world_packets;              // this frame's packets with world vertices
uint32_t pool_min = 1, pool_max = 0;              // the primitive pool, from the last two 3D frames
uint32_t frame_pool_min = 1, frame_pool_max = 0;  // ... from the last one

// A top corner: the top 40%, that half, and within 80 pixels of the side (the pause menu's score
// is farther in; a wide ammo icon reaches past the outer quarter).
int8_t corner_anchor(const FrameList &list, int min_x, int max_x, int max_y) {
    int width = list.area_x2 - list.area_x1 + 1, height = list.area_y2 - list.area_y1 + 1;
    if (max_y > list.area_y1 + height * 2 / 5) return 0;
    if (max_x < list.area_x1 + width / 2 && min_x <= list.area_x1 + 80) return -1;
    if (min_x > list.area_x1 + width / 2 && max_x >= list.area_x2 - 80) return 1;
    return 0;
}

// At the end of a frame's list: group its HUD primitives into elements and pick their anchors.
void layout_hud(const FrameList &list) {
    if (world_packets.size() >= 32) {
        // The pool: the biggest run of world packets without a 32 KB gap (a world primitive kept
        // elsewhere doesn't count), with some margin, together with the previous frame's (the
        // game may alternate buffers).
        std::sort(world_packets.begin(), world_packets.end());
        size_t best = 0, best_n = 0;
        for (size_t i = 0; i < world_packets.size();) {
            size_t j = i + 1;
            while (j < world_packets.size() && world_packets[j] - world_packets[j - 1] < 0x8000) j++;
            if (j - i > best_n) {
                best = i;
                best_n = j - i;
            }
            i = j;
        }
        uint32_t lo = world_packets[best] - 0x2000, hi = world_packets[best + best_n - 1] + 0x2000;
        pool_min = frame_pool_max ? std::min(lo, frame_pool_min) : lo;
        pool_max = std::max(hi, frame_pool_max);
        frame_pool_min = lo;
        frame_pool_max = hi;
    }
    world_packets.clear();
    if (hud_prims.empty()) return;
    // Elements: primitives that touch on screen, side by side on a row (glyphs of a line, the
    // ammo count's digits and icon) or overlapping (the compass's ring and disc).
    size_t n = hud_prims.size();
    std::vector<uint32_t> parent(n);
    for (size_t i = 0; i < n; i++) parent[i] = uint32_t(i);
    auto root = [&](uint32_t i) {
        while (parent[i] != i) i = parent[i] = parent[parent[i]];
        return i;
    };
    for (size_t i = 0; i < n; i++)
        for (size_t j = i + 1; j < n; j++) {
            const HudPrim &a = hud_prims[i], &b = hud_prims[j];
            if (a.min_x <= b.max_x + 24 && b.min_x <= a.max_x + 24 && a.min_y <= b.max_y + 2 && b.min_y <= a.max_y + 2)
                parent[root(uint32_t(i))] = root(uint32_t(j));
        }
    std::vector<HudPrim> elements(n, HudPrim{0, INT_MAX, INT_MIN, INT_MAX, INT_MIN});
    for (size_t i = 0; i < n; i++) {
        HudPrim &e = elements[root(uint32_t(i))];
        const HudPrim &p = hud_prims[i];
        e.min_x = std::min(e.min_x, p.min_x);
        e.max_x = std::max(e.max_x, p.max_x);
        e.min_y = std::min(e.min_y, p.min_y);
        e.max_y = std::max(e.max_y, p.max_y);
    }
    hud_boxes.clear();
    for (size_t i = 0; i < n; i++) {
        if (root(uint32_t(i)) != i) continue;
        const HudPrim &e = elements[i];
        int8_t anchor = corner_anchor(list, e.min_x, e.max_x, e.max_y);
        hud_boxes.push_back({e.min_x - list.area_x1, e.max_x - list.area_x1, e.min_y - list.area_y1, e.max_y - list.area_y1,
                             anchor});
        static const char *dump = getenv("VALOR_TRACE_HUD");
        if (dump)
            log("hud element x %d..%d y %d..%d anchor %d (pool %08X..%08X)", e.min_x, e.max_x, e.min_y, e.max_y, anchor,
                pool_min, pool_max);
    }
    hud_prims.clear();
}

void begin_hud(const int32_t *src, int n, int min_x, int max_x, int min_y, int max_y, bool polygon) {
    hud.active = hud.is_hud = hud.world = false;
    if (!recording || !recording->tick || !recording->has_area) return;
    bool projected = false, world = false;
    for (int i = 0; i < n; i++) {
        projected |= src[i] != -1;
        world |= prim_recs[i] >= 0;
    }
    if (world) {
        world_packets.push_back(gpu_packet_addr);
        recording->world_prims++;
        hud.world = !prim_other;
    }
    if (projected || world) return;
    if (recording->tick->frame + 3 < gpu.frames || recording->tick->vertices.size() < 64) return;  // 2D screen
    uint32_t p = gpu_packet_addr;
    if (polygon && p >= pool_min && p <= pool_max) {
        hud.world = true;
        return;
    }
    int width = recording->area_x2 - recording->area_x1 + 1;
    if (max_x - min_x >= width * 9 / 10) return;
    hud.is_hud = true;
    if (!config.widescreen_hud || gte_squeeze >= 1) return;
    hud_prims.push_back({p, min_x, max_x, min_y, max_y});
    hud.active = true;
    float left = float(recording->area_x1), right = float(recording->area_x2 + 1);
    hud.center = (left + right) * 0.5f;
    if (min_x <= left) {
        hud.center = left;  // touching an edge (borders): stays on it
    } else if (max_x >= right - 1) {
        hud.center = right;
    } else if (config.hud_edges) {
        int x0 = min_x - recording->area_x1, x1 = max_x - recording->area_x1;
        int y0 = min_y - recording->area_y1, y1 = max_y - recording->area_y1;
        int anchor = 0;
        bool inside = false;
        for (const HudBox &b : hud_boxes)
            if (x0 >= b.min_x - 24 && x1 <= b.max_x + 24 && y0 >= b.min_y - 2 && y1 <= b.max_y + 2) {
                anchor = b.anchor;
                inside = true;
                break;
            }
        if (!inside) anchor = corner_anchor(*recording, min_x, max_x, max_y);
        if (anchor) hud.center = anchor < 0 ? left : right;
    }
    hud.factor = float(gte_squeeze);
}

VertexSource source_for(int32_t rec, uint32_t key) {
    VertexSource s;
    if (rec >= 0) s.rec = rec;
    else if (rec == -1) s.group = prim_group;
    s.key = key;
    s.hud = hud.is_hud;
    s.world = hud.world;
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
    st.depth = prim_depth_mode;
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
    gv.w = 1;
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

// Where a camera-space point projects, without the GTE's limits (saturating divide, clamping).
void project_exact(const InterpVertex &p, double x, double y, double z, float &sx, float &sy) {
    double q = p.h / std::max(z, 1.0);
    sx = float(p.ofx + q * x * p.kx);
    sy = float(p.ofy + q * y * p.ky);
}

// Sub-pixel precision (config.subpixel): the GTE rounds projected positions down to whole pixels,
// which makes vertices wobble as things move. Projected vertices are drawn where they really
// project (from their recorded camera-space position); others in the same primitive move by the
// average of that correction, and a point corrected once in a frame is corrected the same way
// in every primitive using it.

void precise_positions(const Vertex *const *v, int n, float *x, float *y) {
    for (int i = 0; i < n; i++) {
        x[i] = float(v[i]->x);
        y[i] = float(v[i]->y);
    }
    if (!config.subpixel || !recording || !recording->tick) return;
    const auto &tv = recording->tick->vertices;
    float cx = 0, cy = 0;
    int corrected = 0;
    bool done[4] = {};
    for (int i = 0; i < n; i++) {
        float dx, dy;
        if (prim_any[i] >= 0) {
            const InterpVertex &p = tv[prim_any[i]];
            if (p.z < p.h * 0.5f) continue;  // the GTE's divide saturates here
            float sx, sy;
            project_exact(p, p.x, p.y, p.z, sx, sy);
            dx = sx - float(int16_t(v[i]->key & 0xFFFF));
            dy = sy - float(int16_t(v[i]->key >> 16));
            if (std::abs(dx) > 1.5f || std::abs(dy) > 1.5f) continue;  // clamped or otherwise off
            subpixel_points[v[i]->key] = {dx, dy};
        } else {
            auto it = subpixel_points.find(v[i]->key);
            if (it == subpixel_points.end()) continue;
            dx = it->second.first;
            dy = it->second.second;
        }
        x[i] += dx;
        y[i] += dy;
        cx += dx;
        cy += dy;
        corrected++;
        done[i] = true;
    }
    if (!corrected) return;
    for (int i = 0; i < n; i++)
        if (!done[i]) {
            x[i] += cx / float(corrected);
            y[i] += cy / float(corrected);
        }
}

// Near clipping. The GTE's divide saturates for points closer than H/2 and can't project points
// behind the camera, so the game drops polygons reaching behind it and relies on its narrow view
// to keep the gaps off screen. A wider view shows them at its edges as holes in nearby walls.
// A world triangle with a vertex that close is drawn from its camera-space vertices instead,
// clipped at a near plane and projected exactly (texture coordinates and colors interpolated
// along the cut edges). game.cpp lets the game submit the ones reaching behind the camera.
// Clip a near triangle with its corners at `q` (camera space) at the near plane and project it.
// Drawn as one or two triangles, textured perspective-correct (the PS1's affine mapping smears
// badly this close). Splitting them instead would leave T-junctions along edges shared with
// polygons drawn normally, which crack. False when it is entirely behind the near plane.
bool clip_near(const NearTri &t, const double q[3][3], NearCorner out[NEAR_CORNERS]) {
    constexpr double NEAR_Z = 16;
    struct Point { double x, y, z; float u, v, r, g, b, cx, cy; };
    Point in[3], pts[4];
    for (int i = 0; i < 3; i++)
        in[i] = {q[i][0], q[i][1], q[i][2], t.u[i], t.v[i], t.r[i], t.g[i], t.b[i], t.cx[i], t.cy[i]};
    int n = 0;
    for (int i = 0; i < 3; i++) {
        const Point &a = in[i], &b = in[(i + 1) % 3];
        bool a_in = a.z >= NEAR_Z, b_in = b.z >= NEAR_Z;
        if (a_in) pts[n++] = a;
        if (a_in != b_in) {
            double d = (NEAR_Z - a.z) / (b.z - a.z);
            auto mix = [d](double x, double y) { return x + (y - x) * d; };
            pts[n++] = {mix(a.x, b.x), mix(a.y, b.y), NEAR_Z, float(mix(a.u, b.u)), float(mix(a.v, b.v)),
                        float(mix(a.r, b.r)), float(mix(a.g, b.g)), float(mix(a.b, b.b)), 0, 0};
        }
    }
    if (n < 3) return false;
    static constexpr int order[2][3] = {{0, 1, 2}, {0, 2, 3}};
    for (int k = 0; k < NEAR_CORNERS; k++) {
        const Point &p = pts[k / 3 < n - 2 ? order[k / 3][k % 3] : 0];
        NearCorner &o = out[k];
        project_exact(t.p[0], p.x, p.y, p.z, o.x, o.y);
        o.x += p.cx + t.ox;
        o.y += p.cy + t.oy;
        o.u = p.u; o.v = p.v; o.r = p.r; o.g = p.g; o.b = p.b;
        o.z = float(p.z);
    }
    return true;
}

bool near_triangle(const DrawState &s, const Vertex *const vs[3], const int32_t src[3], bool dither) {
    if (!recording || !recording->tick) return false;
    const auto &tv = recording->tick->vertices;
    bool near = false, risky = false;
    for (int i = 0; i < 3; i++) {
        if (src[i] < 0 || tv[src[i]].h != float(config.world_h)) return false;
        near |= tv[src[i]].z < tv[src[i]].h * 0.5f;
    }
    if (!near && interp_presenting()) {
        // In-between frames turn the view up to 30 degrees ahead and move it: a corner close by, or
        // far enough to the side to end up beside the camera, can cross the near plane then.
        double tan_h, tan_v;
        gte_world_view(tan_h, tan_v);
        const double side = std::tan(55 * M_PI / 180);
        for (int i = 0; i < 3 && tan_h > 0; i++) {
            const InterpVertex &p = tv[src[i]];
            double sx = std::abs(p.x) * p.h * p.kx / (p.z * p.ofx) * tan_h;  // real tangents off the axis
            double sy = std::abs(p.y) * p.h * p.ky / (p.z * p.ofy) * tan_v;
            risky |= p.z < p.h || std::hypot(sx, sy) > side;
        }
    }
    if (!near && !risky) return false;
    NearTri t;
    t.perspective = near;
    for (int i = 0; i < 3; i++) {
        t.p[i] = tv[src[i]];
        t.u[i] = float(vs[i]->u); t.v[i] = float(vs[i]->v);
        t.r[i] = float(vs[i]->r); t.g[i] = float(vs[i]->g); t.b[i] = float(vs[i]->b);
        // Without sub-pixel precision, corners the GTE projected properly keep its (integer)
        // position, so edges shared with triangles drawn normally line up. (Cut points are on edges
        // reaching behind the camera, which only other near-clipped triangles share.)
        t.cx[i] = t.cy[i] = 0;
        if (!config.subpixel && t.p[i].z >= t.p[i].h * 0.5f) {
            float sx, sy;
            project_exact(t.p[i], t.p[i].x, t.p[i].y, t.p[i].z, sx, sy);
            t.cx[i] = float(int16_t(vs[i]->key & 0xFFFF)) - sx;
            t.cy[i] = float(int16_t(vs[i]->key >> 16)) - sy;
            if (std::abs(t.cx[i]) > 1.5f || std::abs(t.cy[i]) > 1.5f) t.cx[i] = t.cy[i] = 0;  // clamped by the GTE
        }
    }
    // The drawing offset: submitted coordinates are the packet's plus the offset.
    t.ox = float(vs[0]->x - int16_t(vs[0]->key & 0xFFFF));
    t.oy = float(vs[0]->y - int16_t(vs[0]->key >> 16));
    double q[3][3];
    for (int i = 0; i < 3; i++) {
        q[i][0] = t.p[i].x; q[i][1] = t.p[i].y; q[i][2] = t.p[i].z;
    }
    NearCorner out[NEAR_CORNERS];
    if (!clip_near(t, q, out)) return true;  // entirely behind the near plane
    int32_t index = int32_t(recording->near.size());
    recording->near.push_back(t);
    bool depth = config.depth_buffer && t.p[0].translated;
    float bias = 0;
    if (depth) {
        float z = out[0].z;
        begin_depth(&z, 1, s.semi);  // for the mode and bias
        bias = prim_bias;
    }
    // Always two triangles' worth, so in-between frames can clip it differently.
    submit(s, [&](uint16_t pass) {
        uint16_t f = base_flags(s, dither) | pass | F_FILTER;
        for (int i = 0; i < NEAR_CORNERS; i++) {
            const NearCorner &o = out[i];
            batch.push_back(make_vertex(s, o.x, o.y, o.u, o.v, int(std::lround(o.r)), int(std::lround(o.g)),
                                        int(std::lround(o.b)), f));
            batch.back().w = t.perspective ? std::max(o.z, 1.0f) : 1.0f;
            VertexSource vs_;
            vs_.near = index;
            vs_.near_slot = int8_t(i);
            if (depth) {
                batch.back().depth = depth_of(o.z) - bias;
                vs_.depth_bias = bias;
            }
            batch_sources.push_back(vs_);
        }
    });
    prim_depth_mode = 0;
    return true;
}

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
    begin_hud(src, 3, std::min({a.x, b.x, c.x}), std::max({a.x, b.x, c.x}), std::min({a.y, b.y, c.y}),
              std::max({a.y, b.y, c.y}), true);
    bool dither = s.dither && (s.shaded || (s.textured && !s.raw));
    if (!hud.is_hud && near_triangle(s, vs, src, dither)) {
        hud.active = hud.is_hud = hud.world = false;
        return;
    }
    float px[3], py[3];
    precise_positions(vs, 3, px, py);
    if (!hud.is_hud && recording && recording->tick && src[0] >= 0 && src[1] >= 0 && src[2] >= 0) {
        const auto &tv = recording->tick->vertices;
        if (tv[src[0]].translated && tv[src[1]].translated && tv[src[2]].translated) {
            float z[3] = {tv[src[0]].z, tv[src[1]].z, tv[src[2]].z};
            begin_depth(z, 3, s.semi);
        }
    }
    bool projected = src[0] != -1 || src[1] != -1 || src[2] != -1;
    submit(s, [&](uint16_t pass) {
        uint16_t f = base_flags(s, dither) | pass | (projected ? F_FILTER : 0);
        for (int i = 0; i < 3; i++) {
            const Vertex *v = vs[i];
            batch.push_back(make_vertex(s, px[i], py[i], float(v->u), float(v->v), v->r, v->g, v->b, f));
            batch_sources.push_back(source_for(src[i], v->key));
            if (prim_depth_mode) {
                batch.back().depth = prim_depth[i];
                batch_sources.back().depth_bias = prim_bias;
            }
        }
    });
    prim_depth_mode = 0;
    hud.active = hud.is_hud = hud.world = false;
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
    begin_hud(src, 2, std::min(a.x, b.x), std::max(a.x, b.x), std::min(a.y, b.y), std::max(a.y, b.y), false);
    bool dither = s.dither && s.shaded;
    submit(s, [&](uint16_t pass) {
        uint16_t f = base_flags(s, dither) | pass;
        for (int i : {0, 0, 1, 0, 1, 1}) batch_sources.push_back(source_for(src[i], vs[i]->key));
        GLVertex p0 = make_vertex(s, ax - ex - nx, ay - ey - ny, 0, 0, a.r, a.g, a.b, f);
        GLVertex p1 = make_vertex(s, ax - ex + nx, ay - ey + ny, 0, 0, a.r, a.g, a.b, f);
        GLVertex p2 = make_vertex(s, bx + ex - nx, by + ey - ny, 0, 0, b.r, b.g, b.b, f);
        GLVertex p3 = make_vertex(s, bx + ex + nx, by + ey + ny, 0, 0, b.r, b.g, b.b, f);
        for (const GLVertex *v : {&p0, &p1, &p2, &p1, &p3, &p2}) batch.push_back(*v);
    });
    hud.active = hud.is_hud = hud.world = false;
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
    begin_hud(&src, 1, base.x, base.x + w, base.y, base.y + h, false);
    submit(s, [&](uint16_t pass) {
        uint16_t f = base_flags(s, false) | pass;
        for (int i = 0; i < 6; i++) batch_sources.push_back(source_for(src, base.key));
        auto vx = [&](float x, float y, float u, float v) { return make_vertex(s, x, y, u, v, base.r, base.g, base.b, f); };
        GLVertex p0 = vx(x0, y0, u0, v0), p1 = vx(x1, y0, u0 + w, v0), p2 = vx(x0, y1, u0, v0 + h),
                 p3 = vx(x1, y1, u0 + w, v0 + h);
        for (const GLVertex *v : {&p0, &p1, &p2, &p1, &p3, &p2}) batch.push_back(*v);
    });
    hud.active = hud.is_hud = hud.world = false;
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
    make_target(target_tex, target_fbo, &target_depth);
    make_target(temp_tex, temp_fbo);
}

static void free_targets() {
    GLuint tex[2] = {target_tex, temp_tex}, fbo[2] = {target_fbo, temp_fbo};
    glDeleteTextures(2, tex);
    glDeleteFramebuffers(2, fbo);
    glDeleteRenderbuffers(1, &target_depth);
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
    u_filter = glGetUniformLocation(program, "u_filter");
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
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, sizeof(GLVertex), off(offsetof(GLVertex, depth)));
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, sizeof(GLVertex), off(offsetof(GLVertex, w)));

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
        finish_list();
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
    // Mouse look shows at once instead of with the ticks. A tick's camera shows the mouse look the
    // game took before the previous tick (its control code runs after the projections), so: the
    // rest of this tick's turn toward the next instead of easing it in, all of the next tick's
    // (seen only in the one after), and the motion the game hasn't taken yet.
    double yaw = 0, pitch = 0;
    if (next && !game_aiming()) {
        kbm_pending_look(yaw, pitch);
        yaw += list->tick->look_yaw * (1 - alpha) + next->look_yaw;
        pitch += list->tick->look_pitch * (1 - alpha) + next->look_pitch;
    }
    bool ok = next && (alpha > 0 || yaw != 0 || pitch != 0) && interp_motion(*list->tick, *next, alpha, yaw, pitch, motion);
    if (!ok && !list->moved) return;  // the target already shows the list as drawn
    list->moved = ok;
    if (static bool trace = getenv("VALOR_TRACE_INTERP"); trace) {
        size_t rec = 0, group = 0;
        for (const auto &s : list->sources) { rec += s.rec >= 0; group += s.group >= 0; }
        log("interp: sources none %llu ambiguous %llu other %llu matched %llu", (unsigned long long)source_stats[0],
            (unsigned long long)source_stats[1], (unsigned long long)source_stats[2], (unsigned long long)source_stats[3]);
        memset(source_stats, 0, sizeof source_stats);
        log("interp: t=%.2f tick %llu alpha %.2f look %.2f,%.2f ok %d: %zu vertices, %zu matched, %zu by group, "
            "%zu projections, %zu transforms, %zu moving",
            vblank_time, (unsigned long long)list->tick->seq, alpha, yaw, pitch, ok, list->sources.size(), rec, group,
            list->tick->vertices.size(), list->tick->groups.size(), ok ? motion.objects.size() : 0);
    }

    // Screen movement of every projection this tick.
    const auto &pv = list->tick->vertices;
    static std::vector<float> dx, dy, qz;
    static std::vector<uint8_t> valid;
    dx.assign(pv.size(), 0);
    dy.assign(pv.size(), 0);
    qz.assign(pv.size(), 0);
    valid.assign(pv.size(), 0);
    // Where a projected point is at this moment, in camera space.
    auto move = [&](const InterpVertex &p, double q[3]) {
        int32_t object = p.group < int32_t(motion.group_object.size()) ? motion.group_object[p.group] : -1;
        if (object >= 0) {
            const auto &o = motion.objects[object];
            for (int r = 0; r < 3; r++) q[r] = o.m[r][0] * p.v[0] + o.m[r][1] * p.v[1] + o.m[r][2] * p.v[2] + o.t[r];
        } else {
            for (int r = 0; r < 3; r++)
                q[r] = motion.a[r][0] * p.x + motion.a[r][1] * p.y + motion.a[r][2] * p.z + (p.translated ? motion.b[r] : 0);
        }
    };
    auto turn = [&](double q[3]) {
        if (!motion.turned) return;
        double t[3];
        for (int r = 0; r < 3; r++) t[r] = motion.look[r][0] * q[0] + motion.look[r][1] * q[1] + motion.look[r][2] * q[2];
        memcpy(q, t, sizeof t);
    };
    // A world vertex's projection parameters, for points without a projection of their own.
    const InterpVertex *view = nullptr;
    for (const InterpVertex &p : pv)
        if (p.h == float(config.world_h)) { view = &p; break; }
    for (size_t i = 0; i < pv.size(); i++) {
        const InterpVertex &p = pv[i];
        if (!ok || p.h != float(config.world_h)) continue;
        double q[3];
        move(p, q);
        turn(q);
        // Projected as near-clipped triangles project (so points they share stay together),
        // unless the GTE's divide saturated where it was drawn.
        auto project = p.z >= p.h * 0.5f ? project_exact : interp_project;
        float x0, y0, x1, y1;
        project(p, p.x, p.y, p.z, x0, y0);
        project(p, q[0], q[1], q[2], x1, y1);
        // From where it was drawn: the GTE clamps the screen position (points far off screen).
        x0 = std::clamp(x0, -1024.0f, 1023.0f);
        y0 = std::clamp(y0, -1024.0f, 1023.0f);
        dx[i] = x1 - x0;
        dy[i] = y1 - y0;
        qz[i] = float(q[2]);
        valid[i] = 1;
    }
    // A point without a projection: where it is in camera space, from its primitive's projected
    // vertices (depth interpolated as 1/z across the polygon, which is linear on screen), moved
    // like them. Without any, as a direction: turning the camera moves it the same at any depth.
    // Points on a moving object, or next to points the GTE couldn't project properly, move with
    // the primitive's vertices on average.
    auto unprojected_motion = [&](const VertexSource &s, float &mx, float &my) {
        mx = my = 0;
        if (!ok) return;
        int32_t r[4];
        int n = 0;
        bool object = false;
        if (s.group >= 0)
            for (int32_t k : list->groups[s.group]) {
                if (k >= 0 && pv[k].h != float(config.world_h)) return;  // the weapon: stays put
                if (k >= 0 && valid[k]) {
                    r[n++] = k;
                    const InterpVertex &p = pv[k];
                    object |= p.group < int32_t(motion.group_object.size()) && motion.group_object[p.group] >= 0;
                    object |= p.z < p.h * 0.5f;  // its drawn position isn't where it projects
                }
            }
        if (object || !view) {
            for (int i = 0; i < n; i++) { mx += dx[r[i]]; my += dy[r[i]]; }
            if (n) { mx /= float(n); my /= float(n); }
            return;
        }
        const InterpVertex &v = n ? pv[r[0]] : *view;
        double gx = int16_t(s.key & 0xFFFF), gy = int16_t(s.key >> 16);
        auto screen = [&](int32_t k, double &x, double &y) {
            x = v.ofx + v.h * v.kx * pv[k].x / pv[k].z;
            y = v.ofy + v.h * v.ky * pv[k].y / pv[k].z;
        };
        double inv_z = 0;
        bool translated = n > 0 && pv[r[0]].translated;  // false: only the camera's turn
        if (n >= 3) {
            double x[3], y[3];
            for (int i = 0; i < 3; i++) screen(r[i], x[i], y[i]);
            double d = (y[1] - y[2]) * (x[0] - x[2]) + (x[2] - x[1]) * (y[0] - y[2]);
            if (std::abs(d) > 1e-3) {
                double w0 = ((y[1] - y[2]) * (gx - x[2]) + (x[2] - x[1]) * (gy - y[2])) / d;
                double w1 = ((y[2] - y[0]) * (gx - x[2]) + (x[0] - x[2]) * (gy - y[2])) / d;
                inv_z = w0 / pv[r[0]].z + w1 / pv[r[1]].z + (1 - w0 - w1) / pv[r[2]].z;
            } else {
                n = 2;
            }
        }
        if (n == 2) {
            double x[2], y[2];
            for (int i = 0; i < 2; i++) screen(r[i], x[i], y[i]);
            double ex = x[1] - x[0], ey = y[1] - y[0], len = ex * ex + ey * ey;
            double t = len > 1e-6 ? std::clamp(((gx - x[0]) * ex + (gy - y[0]) * ey) / len, 0.0, 1.0) : 0.5;
            inv_z = (1 - t) / pv[r[0]].z + t / pv[r[1]].z;
        } else if (n == 1) {
            inv_z = 1 / pv[r[0]].z;
        }
        double z = inv_z > 1e-6 ? 1 / inv_z : 0;
        if (n > 0) {
            // Far outside its vertices (extrapolated): not where they put it.
            double lo = 1e9, hi = 0;
            for (int i = 0; i < n; i++) {
                lo = std::min(lo, double(pv[r[i]].z));
                hi = std::max(hi, double(pv[r[i]].z));
            }
            if (z < lo * 0.5 || z > hi * 2) {
                for (int i = 0; i < n; i++) { mx += dx[r[i]]; my += dy[r[i]]; }
                mx /= float(n);
                my /= float(n);
                return;
            }
        }
        if (z <= 0 || z > 1e6) { z = 1; translated = false; }
        double p[3] = {(gx - v.ofx) * z / (v.kx * v.h), (gy - v.ofy) * z / (v.ky * v.h), z}, q[3];
        for (int i = 0; i < 3; i++)
            q[i] = motion.a[i][0] * p[0] + motion.a[i][1] * p[1] + motion.a[i][2] * p[2] + (translated ? motion.b[i] : 0);
        turn(q);
        if (q[2] < 1) {
            for (int i = 0; i < n; i++) { mx += dx[r[i]]; my += dy[r[i]]; }
            if (n) { mx /= float(n); my /= float(n); }
            return;
        }
        mx = float(v.ofx + v.h * v.kx * q[0] / q[2] - gx);
        my = float(v.ofy + v.h * v.ky * q[1] / q[2] - gy);
    };
    // A screen point resolved to a projection in one primitive moves the same way in all of them
    // (with several candidates, a primitive may not have been able to pick one).
    static std::unordered_map<uint32_t, int32_t> point_rec;
    point_rec.clear();
    for (const VertexSource &s : list->sources)
        if (s.rec >= 0) point_rec.try_emplace(s.key, s.rec);
    static std::vector<GLVertex> moved;
    static std::unordered_map<uint32_t, std::pair<float, float>> unprojected;
    unprojected.clear();
    moved = list->vertices;
    static std::vector<NearCorner> near_out;
    static std::vector<uint64_t> near_done;
    static uint64_t near_stamp;
    near_stamp++;
    near_out.resize(list->near.size() * NEAR_CORNERS);
    near_done.resize(std::max(near_done.size(), list->near.size()));
    for (size_t i = 0; i < moved.size(); i++) {
        const VertexSource &s = list->sources[i];
        if (s.near >= 0) {
            // Near-clipped: clipped again where its corners are now (a turn can take a cut point
            // behind the camera), the same way in each pass.
            if (!ok) continue;
            if (near_done[s.near] != near_stamp) {
                near_done[s.near] = near_stamp;
                const NearTri &t = list->near[s.near];
                double q[3][3];
                for (int k = 0; k < 3; k++) {
                    move(t.p[k], q[k]);
                    turn(q[k]);
                }
                NearCorner *out = &near_out[size_t(s.near) * NEAR_CORNERS];
                if (!clip_near(t, q, out))
                    for (int k = 0; k < NEAR_CORNERS; k++) out[k] = {-4096, -4096, 0, 0, 0, 0, 0, 1};
            }
            const NearCorner &o = near_out[size_t(s.near) * NEAR_CORNERS + s.near_slot];
            moved[i].x = o.x;
            moved[i].y = o.y;
            moved[i].u = o.u;
            moved[i].v = o.v;
            moved[i].r = uint8_t(std::lround(o.r));
            moved[i].g = uint8_t(std::lround(o.g));
            moved[i].b = uint8_t(std::lround(o.b));
            moved[i].w = list->near[s.near].perspective ? std::max(o.z, 1.0f) : 1.0f;
            if (s.depth_bias >= 0) moved[i].depth = depth_of(o.z) - s.depth_bias;
            continue;
        }
        float mx = 0, my = 0;
        int32_t rec = s.rec;
        if (rec < 0 && s.world) {
            auto it = point_rec.find(s.key);
            if (it != point_rec.end()) rec = it->second;
        }
        if (rec >= 0 && valid[rec]) {
            mx = dx[rec];
            my = dy[rec];
            if (s.depth_bias >= 0) moved[i].depth = depth_of(qz[rec]) - s.depth_bias;
        } else if (s.group >= 0 || (s.world && ok)) {
            // No projection (a point the CPU computed, like a clipped edge or a subdivided
            // polygon's): moved the same way wherever this point is used, so neighbours don't
            // crack apart.
            auto [it, added] = unprojected.try_emplace(s.key);
            if (added) {
                unprojected_motion(s, mx, my);
                it->second = {mx, my};
            }
            mx = it->second.first;
            my = it->second.second;
        }
        moved[i].x += mx;
        moved[i].y += my;
    }
    flush();
    clear_depth();
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
