#pragma once

#include "em/math/vector.h"

using namespace em;

struct World
{
    ivec2 mouse_pos;

    void Tick();
    void Render();
};
