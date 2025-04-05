#pragma once

#include "em/math/vector.h"

using namespace em;

static constexpr ivec2 screen_size = ivec2(1920, 1080) / 4;

struct DrawSettings
{
    fvec4 color;
    ivec2 tex_pos;
    fvec3 factors;

    // Low level, mixed texture and color.
    DrawSettings(ivec2 tex_pos, fvec4 color, float mix_tex, float mix_tex_alpha, float beta = 1)
        : color(color), tex_pos(tex_pos), factors(mix_tex, mix_tex_alpha, beta)
    {}

    // Only color.
    DrawSettings(fvec4 color, float beta = 1)
        : DrawSettings(ivec2(), color, 0, 0, beta)
    {}

    // Only texture.
    DrawSettings(ivec2 tex_pos, float alpha = 1, float beta = 1)
        : DrawSettings(tex_pos, fvec4(0), 1, alpha, beta)
    {}

};

void DrawRect(ivec2 pos, ivec2 size, const DrawSettings &settings);

inline void DrawRectAbs(ivec2 pos_a, ivec2 pos_b, const DrawSettings &settings)
{
    DrawRect(pos_a, pos_b - pos_a, settings);
}

// The rect expands outwards from those coordinates.
inline void DrawRectHollow(ivec2 pos, ivec2 size, ivec2 size_top_left, ivec2 size_bottom_right, const DrawSettings &settings)
{
    DrawRect(pos - size_top_left, ivec2(size.x + size_top_left.x + size_bottom_right.x, size_top_left.y), settings);
    DrawRect(ivec2(pos.x - size_top_left.x, pos.y), ivec2(size_top_left.x, size.y), settings);
    DrawRect(ivec2(pos.x + size.x, pos.y), ivec2(size_bottom_right.x, size.y), settings);
    DrawRect(ivec2(pos.x - size_top_left.x, pos.y + size.y), ivec2(size.x + size_top_left.x + size_bottom_right.x, size_bottom_right.y), settings);
}

// A simple overload with the same width on all four sides.
inline void DrawRectHollow(ivec2 pos, ivec2 size, int width, const DrawSettings &settings)
{
    DrawRectHollow(pos, size, ivec2(width), ivec2(width), settings);
}
