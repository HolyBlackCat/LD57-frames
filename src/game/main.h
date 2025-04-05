#pragma once

#include "em/math/vector.h"

using namespace em;

void DrawRectLow(ivec2 pos, ivec2 size, fvec2 tex_pos, fvec4 color, float mix_tex, float mix_tex_alpha, float beta = 1);

inline void DrawRectTex(ivec2 pos, ivec2 size, fvec2 tex_pos, float alpha = 1, float beta = 1)
{
    DrawRectLow(pos, size, tex_pos, fvec4(0), 1, alpha, beta);
}

inline void DrawRectColor(ivec2 pos, ivec2 size, fvec4 color, float beta = 1)
{
    DrawRectLow(pos, size, fvec2(), color, 0, 0, beta);
}
