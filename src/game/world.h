#pragma once

#include "em/meta/copyable_unique_ptr.h"
#include "em/math/vector.h"

using namespace em;

struct World
{
    ivec2 mouse_pos;

    struct State;
    em::Meta::CopyableUniquePtr<State> state;

    World();
    World(const World &);
    World(World &&);
    World &operator=(const World &);
    World &operator=(World &&);
    ~World();

    void Tick();
    void Render();
};
