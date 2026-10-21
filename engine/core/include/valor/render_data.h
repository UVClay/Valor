// Render data from the disc files, for a host engine to draw natively (docs/re/formats.md: TSP
// render tree and faces, TAF TIMs placed in VRAM). Presentation only: nothing here feeds the rules.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "valor/level.h"

namespace valor {

struct RenderVertex {
    int16_t x, y, z;  // world units, y down
    uint8_t r, g, b;
};

struct RenderFace {
    uint16_t v[3];
    uint8_t uv[3][2];
    uint16_t clut;   // GPU CLUT word: x = (clut & 0x3F) * 16, y = clut >> 6
    uint16_t tpage;  // bits 0..11 the GPU tpage; bit 14 semi-transparent
    bool semi() const { return tpage & 0x4000; }
    int blend() const { return (tpage >> 5) & 3; }  // 0 B/2+F/2, 1 B+F, 2 B-F, 3 B+F/4
    int depth() const { return (tpage >> 7) & 3; }  // 0 4-bit, 1 8-bit, 2 15-bit
};

struct RenderChunk {
    int number = 0;
    std::vector<RenderVertex> verts;  // colours as stored (lights not applied)
    std::vector<RenderFace> faces;    // every leaf's faces, in tree order

    // Parses a TSP file's render tree, vertices, colours and faces.
    bool parse(const std::vector<uint8_t>& tsp);
};

// The PS1's 1024 x 512 16-bit VRAM, as the level's TAF TIMs fill it.
struct Vram {
    static constexpr int kW = 1024, kH = 512;
    std::vector<uint16_t> px = std::vector<uint16_t>(kW * kH, 0);

    // taf_load_tims: the TAF's leading TIMs, each uploaded to the place its header names.
    bool load_taf_tims(const std::vector<uint8_t>& taf, int* tims = nullptr);

    // One texture page as seen through a CLUT: 256 x 256 texels, RGBA8. Alpha 0 for the transparent
    // texel (0x0000), 255 for opaque ones, 128 for texels with the STP bit (semi-transparent in a
    // semi-transparent face).
    void decode_page(uint16_t tpage, uint16_t clut, std::vector<uint8_t>& rgba) const;
};

// A level's render data: every chunk, the VRAM, the light colours.
struct LevelRender {
    std::vector<RenderChunk> chunks;
    Vram vram;

    bool load(const std::string& data_dir, const LevelInfo& info, std::string* err);

    // A vertex colour with the animated lights' substitution (red < 40 is a light index; past
    // the level's lights it draws black), using each light's first step.
    void lit_colour(const LevelInfo& info, const RenderVertex& v, uint8_t out[3]) const;

    // The animated light a vertex colour names (red < 40, below the level's light count), or -1.
    static int light_index(const LevelInfo& info, const RenderVertex& v);
};

}  // namespace valor
