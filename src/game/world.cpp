#include "world.h"

#include "main.h"

void World::Tick()
{

}

void World::Render()
{
    DrawRectTex(mouse_pos, ivec2(32), ivec2(0));
    DrawRectColor(mouse_pos + ivec2(20, 10), ivec2(32), fvec4(1,0,0,0.5));
}
