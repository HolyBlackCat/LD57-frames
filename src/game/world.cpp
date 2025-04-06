#include "world.h"

#include "SDL3/SDL_keyboard.h"
#include "em/macros/utils/lift.h"
#include "em/macros/utils/named_loops.h"
#include "audio/global_sound_loader.h"
#include "main.h"

#include <SDL3/SDL_mouse.h>

#include <cassert>
#include <cmath>
#include <string>

static constexpr int tile_size = 16;

struct Mouse
{
    ivec2 pos;

    bool is_down = false;
    bool is_down_prev = false;

    [[nodiscard]] bool IsPressed() const {return is_down && !is_down_prev;}
    [[nodiscard]] bool IsReleased() const {return !is_down && is_down_prev;}
};
static Mouse mouse;

struct Key
{
    bool is_down = false;
    bool is_down_prev = false;

    [[nodiscard]] bool IsPressed() const {return is_down && !is_down_prev;}
};

struct Keys
{
    Key left;
    Key right;
    Key jump;
};
static Keys keys;


struct FrameType
{
    ivec2 tex_pos; // Measured in tiles.
    std::vector<std::string> tiles;

    FrameType(ivec2 tex_pos, std::vector<std::string> tiles)
        : tex_pos(tex_pos), tiles(std::move(tiles))
    {}

    [[nodiscard]] ivec2 TileSize() const
    {
        if (tiles.empty())
            return ivec2();
        return vec2(tiles.front().size(), tiles.size()).to<int>();
    }

    [[nodiscard]] ivec2 PixelSize() const
    {
        return TileSize() * tile_size;
    }

    [[nodiscard]] ivec2 GetTopLeftCorner(ivec2 pos) const
    {
        return pos - PixelSize() / 2;
    }
};

namespace Frames
{
    static const FrameType
        flower_island(ivec2(0,0), {
            "-----",
            "-----",
            "-----",
            "--1--",
            "-###-",
            "-----",
        }),
        vortex(ivec2(5,0), {
            "-----",
            "-----",
            "-----",
            "-###-",
        });
}

enum class SpawnedEntity
{
    none,
    player,
};

struct Frame
{
    const FrameType *type = nullptr;

    ivec2 pos;

    bool hovered = false;

    float hover_time = 0;

    bool dragged = false;
    ivec2 drag_offset_relative_to_mouse;


    // Which entity this frame can spawn at the `1` marker.
    SpawnedEntity spawned_entity_type;

    bool did_spawn_entity = false;
    // Pixel offset relative to `pos` to the spawned entity.
    ivec2 offset_to_spawned_entity;


    bool aabb_overlaps_player = false;

    // Ignore collisions because the player got under this frame.
    bool player_is_under_this_frame = false;


    Frame(const FrameType &type, ivec2 pos, SpawnedEntity spawned_entity_type = SpawnedEntity::none)
        : type(&type), pos(pos), spawned_entity_type(spawned_entity_type)
    {}

    [[nodiscard]] ivec2 TopLeftCorner() const
    {
        return type->GetTopLeftCorner(pos);
    }
    [[nodiscard]] ivec2 PixelSize() const
    {
        return type->PixelSize();
    }

    [[nodiscard]] bool WorldPixelIsInRect(ivec2 pixel) const
    {
        ivec2 a = TopLeftCorner();
        ivec2 b = a + PixelSize();
        return pixel.x >= a.x && pixel.y >= a.y && pixel.x < b.x && pixel.y < b.y;
    }

    // -1 = not in AABB, 0 = not solid, 1 = solid
    int QueryWorldPixel(ivec2 pixel) const
    {
        if (!WorldPixelIsInRect(pixel))
            return -1;

        ivec2 coord = (pixel - TopLeftCorner()) / tile_size;
        ivec2 num_tiles = type->TileSize();

        if (coord.x < 0 || coord.y < 0 || coord.x >= num_tiles.x || coord.y >= num_tiles.y)
        {
            assert(false); // This shouldn't happen, as we already checked `WorldPixelIsInRect()`.
            return -1;
        }

        return type->tiles.at(std::size_t(coord.y)).at(std::size_t(coord.x)) == '#';
    }

    void Render() const
    {
        ivec2 corner_pos = TopLeftCorner();
        ivec2 pixel_size = PixelSize();

        float under_alpha = player_is_under_this_frame ? 0.5f : 1;

        // Shadow.
        DrawRectAbs(
            corner_pos - ivec2(-1) - (hover_time * ivec2(-1,-1)).map(EM_FUNC(std::round)).to<int>(),
            corner_pos + pixel_size + ivec2(2,2) + (hover_time * ivec2(1,3)).map(EM_FUNC(std::round)).to<int>(),
            fvec4(0, 0, 0, 0.5f * under_alpha)
        );

        // The image.
        DrawRect(corner_pos, pixel_size, {ivec2(0, 128) + tile_size * type->tex_pos, under_alpha, 1});

        // The frame.
        DrawRectHollow(corner_pos, pixel_size, 1, fvec4(0,0,0,under_alpha));

        // Hover indicator.
        if (hovered)
            DrawRectHollow(corner_pos + 1, pixel_size - 2, 1, fvec4(1,1,1,1));
    }
};

struct World::State
{
    std::vector<Frame> frames;


    bool player_exists = false;
    ivec2 player_pos;
    fvec2 player_vel;
    fvec2 player_vel_comp;
    bool player_on_ground = false;
    bool player_on_ground_prev = false;
    bool player_facing_left = false;
    int player_movement_timer = 0;

    bool movement_started = false;


    State()
    {
        frames.emplace_back(
            Frames::flower_island,
            ivec2(0,0),
            SpawnedEntity::player
        );

        frames.emplace_back(
            Frames::vortex,
            ivec2(100,0)
        );

        InitEntitiesFromFrames();
    }

    void InitEntityFromSpecificFrame(Frame &frame)
    {
        if (frame.spawned_entity_type != SpawnedEntity{})
        {
            if (!frame.did_spawn_entity)
            {
                for (int y = 0; y < frame.type->TileSize().y; y++) EM_NAMED_LOOP(outer)
                for (int x = 0; x < frame.type->TileSize().x; x++)
                {
                    if (frame.type->tiles[std::size_t(y)][std::size_t(x)] == '1')
                    {
                        frame.did_spawn_entity = true;
                        frame.offset_to_spawned_entity = frame.TopLeftCorner() + ivec2(x, y) * tile_size + tile_size / 2 - frame.pos;
                        EM_BREAK(outer);
                    }
                }

                if (!frame.did_spawn_entity)
                    throw std::runtime_error("This frame wants to spawn an entity, but has no marker for it.");
            }

            switch (frame.spawned_entity_type)
            {
              case SpawnedEntity::none:
                std::unreachable();
              case SpawnedEntity::player:
                player_exists = true;
                player_pos = frame.pos + frame.offset_to_spawned_entity;
                break;
            }
        }
    }

    void InitEntitiesFromFrames()
    {
        for (Frame &frame : frames)
            InitEntityFromSpecificFrame(frame);
    }


    void Tick()
    {
        static constexpr ivec2 player_hitbox[] = {
            ivec2( 3, 7),
            ivec2(-4, 7),
            ivec2( 3,-3),
            ivec2(-4,-3),
        };


        std::size_t hovered_frame_index = std::size_t(-1);

        bool any_frame_dragged = std::any_of(frames.begin(), frames.end(), EM_MEMBER(.dragged));

        { // Resolve which frame is hovered.
            std::size_t i = frames.size();
            bool found = false;
            while (i-- > 0)
            {
                Frame &frame = frames[i];
                if (!found && (frame.dragged || frame.WorldPixelIsInRect(mouse.pos)))
                {
                    found = true;
                    frame.hovered = true;
                    hovered_frame_index = i;
                }
                else
                {
                    frame.hovered = false;
                }
            }
        }

        { // Update frame hover timers.
            static constexpr float step = 0.15f;

            for (Frame &frame : frames)
            {
                if (frame.hovered)
                {
                    frame.hover_time += step;
                    if (frame.hover_time > 1)
                        frame.hover_time = 1;
                }
                else
                {
                    frame.hover_time -= step;
                    if (frame.hover_time < 0)
                        frame.hover_time = 0;
                }
            }
        }

        { // Dragging.
            // Start drag.
            if (mouse.IsPressed() && hovered_frame_index != std::size_t(-1))
            {
                // Move the activated frame to the end.
                std::rotate(frames.begin() + std::ptrdiff_t(hovered_frame_index), frames.begin() + std::ptrdiff_t(hovered_frame_index) + 1, frames.end());
                // And update the index to match too.
                hovered_frame_index = frames.size() - 1;


                any_frame_dragged = true;
                frames.back().dragged = true;

                frames.back().drag_offset_relative_to_mouse = frames.back().pos - mouse.pos;
            }

            // Finish drag.
            if (!mouse.is_down && any_frame_dragged)
            {
                frames.back().dragged = false;
            }

            // Continue drag.
            if (frames.back().dragged)
            {
                frames.back().pos = mouse.pos + frames.back().drag_offset_relative_to_mouse;

                // Drag the entities with the frames.
                if (!movement_started)
                    InitEntityFromSpecificFrame(frames.back());
            }
        }

        { // Update AABB overlap flags for frames.
            for (Frame &frame : frames)
            {
                frame.aabb_overlaps_player = false;
                for (ivec2 point : player_hitbox)
                {
                    if (frame.WorldPixelIsInRect(player_pos + point))
                    {
                        frame.aabb_overlaps_player = true;
                        break;
                    }
                }

                // Reset the "under frame" flag if no overlap.
                if (!frame.aabb_overlaps_player)
                    frame.player_is_under_this_frame = false;
            }
        }

        // Player.
        if (player_exists)
        {
            static constexpr float
                walk_speed = 1.5f,
                walk_acc = 0.4f,
                walk_dec = 0.4f,
                gravity = 0.14f,
                max_fall_speed = 4
                ;

            int hc = keys.right.is_down - keys.left.is_down;

            // Horizontal control.
            if (hc)
            {
                movement_started = true;

                player_facing_left = hc < 0;

                player_vel.x += hc * walk_acc;
                if (std::abs(player_vel.x) > walk_speed)
                    player_vel.x = player_vel.x > 0 ? walk_speed : -walk_speed;
            }
            else
            {
                bool flip = player_vel.x < 0;
                if (flip)
                    player_vel.x = -player_vel.x;

                if (player_vel.x > walk_dec)
                    player_vel.x -= walk_dec;
                else
                    player_vel.x = 0;

                if (flip)
                    player_vel.x = -player_vel.x;
            }


            auto SolidAtOffset = [&](ivec2 offset, bool update_frames)
            {
                bool ret = false;

                for (ivec2 point : player_hitbox)
                {
                    bool found_aabb_overlap = false;

                    std::size_t i = frames.size();
                    while (i-- > 0)
                    {
                        Frame &frame = frames[i];

                        if (!found_aabb_overlap && !frame.player_is_under_this_frame)
                        {
                            int r = frame.QueryWorldPixel(player_pos + point + offset);

                            if (r >= 0)
                                found_aabb_overlap = true;

                            if (r == 1)
                            {
                                if (!frame.aabb_overlaps_player && update_frames)
                                {
                                    frame.player_is_under_this_frame = true;
                                }
                                else
                                {
                                    ret = true;
                                }
                            }
                        }
                    }
                }

                return ret;
            };

            player_on_ground_prev = player_on_ground;
            player_on_ground = SolidAtOffset(ivec2(0, 1), false);

            if (player_on_ground && !player_on_ground_prev && movement_started)
                audio.Play("landing"_sound, player_pos);


            if (player_on_ground)
            {
                if (keys.jump.IsPressed())
                {
                    movement_started = true;

                    player_vel.y = -3;
                    player_vel_comp.y = 0;

                    audio.Play("jump"_sound, player_pos);
                }
                else
                {
                    if (player_vel.y > 0)
                    {
                        player_vel.y = 0;
                        if (player_vel_comp.y > 0)
                            player_vel_comp.y = 0;
                    }
                }
            }
            else
            {
                player_vel.y += gravity;
                if (player_vel.y > max_fall_speed)
                    player_vel.y = max_fall_speed;
            }


            { // Update position.
                fvec2 vel_with_comp = player_vel + player_vel_comp;
                ivec2 int_vel = vel_with_comp.map(EM_FUNC(std::round)).to<int>();
                player_vel_comp = vel_with_comp - int_vel;
                player_vel_comp *= 0.98f;

                bool moved_x = false;

                while (int_vel != ivec2())
                {
                    for (bool vert : {false, true})
                    {
                        if (int_vel[vert] == 0)
                            continue;

                        ivec2 offset;
                        offset[vert] = int_vel[vert] > 0 ? 1 : -1;

                        if (SolidAtOffset(offset, true))
                        {
                            if (int_vel[vert] * player_vel[vert] > 0)
                            {
                                player_vel[vert] = 0;
                                if (int_vel[vert] * player_vel_comp[vert] > 0)
                                    player_vel_comp[vert] = 0;
                            }
                            int_vel[vert] = 0;
                        }
                        else
                        {
                            int_vel -= offset;
                            player_pos += offset;

                            if (!vert)
                                moved_x = true;
                        }
                    }
                }

                player_pos += int_vel;

                if (moved_x)
                    player_movement_timer++;
                else
                    player_movement_timer = 0;
            }
        }
    }

    void Render() const
    {
        { // Background.
            static constexpr ivec2 bg_size(128);
            ivec2 count = (screen_size + bg_size - 1) / bg_size;
            for (int y = 0; y < count.y; y++)
            for (int x = 0; x < count.x; x++)
            {
                DrawRect(ivec2(x, y) * bg_size - screen_size / 2, bg_size, ivec2(0));
            }
        }

        // Vignette.
        DrawRect(-screen_size / 2, screen_size, {ivec2(544, 754), 0.1f});

        // Frames.
        std::size_t frame_index = 0;
        for (; frame_index < frames.size(); frame_index++)
        {
            if (frames[frame_index].player_is_under_this_frame)
                break;

            frames[frame_index].Render();
        }

        // Player.
        if (player_exists)
        {
            static constexpr int player_sprite_size = 16;

            int pl_state = 0;
            int pl_frame = 0;
            if (player_on_ground)
            {
                if (player_movement_timer > 0)
                {
                    pl_state = 1;
                    pl_frame = player_movement_timer / 3 % 4;
                }
            }
            else
            {
                pl_state = 2;
                if (player_vel.y < -1)
                    pl_frame = 0;
                else if (player_vel.y < -0.5f)
                    pl_frame = 1;
                else if (player_vel.y < 0)
                    pl_frame = 2;
                else if (player_vel.y < 0.5f)
                    pl_frame = 3;
                else
                    pl_frame = 4;
            }

            DrawRect(player_pos - player_sprite_size / 2 + ivec2(0,2), ivec2(player_sprite_size), {ivec2(0, 240) + ivec2(pl_frame, pl_state) * player_sprite_size, 1, 1, player_facing_left});

        }

        // Frames above the player.
        for (; frame_index < frames.size(); frame_index++)
        {
            frames[frame_index].Render();
        }
    }
};


World::World() : state(std::make_unique<State>()) {}
World::World(const World &) = default;
World::World(World &&) = default;
World &World::operator=(const World &) = default;
World &World::operator=(World &&) = default;
World::~World() = default;

void World::Tick()
{
    { // Mouse.
        mouse.pos = mouse_pos;

        mouse.is_down_prev = mouse.is_down;

        SDL_MouseButtonFlags sdl_mouse_flags = SDL_GetMouseState(nullptr, nullptr);
        mouse.is_down = bool(sdl_mouse_flags & SDL_BUTTON_LEFT);
    }

    { // Keys.
        const bool *held_keys = SDL_GetKeyboardState(nullptr);

        keys.left .is_down_prev = keys.left .is_down;
        keys.right.is_down_prev = keys.right.is_down;
        keys.jump .is_down_prev = keys.jump .is_down;

        keys.left.is_down  = held_keys[SDL_SCANCODE_LEFT ] || held_keys[SDL_SCANCODE_A];
        keys.right.is_down = held_keys[SDL_SCANCODE_RIGHT] || held_keys[SDL_SCANCODE_D];
        keys.jump.is_down  = held_keys[SDL_SCANCODE_UP   ] || held_keys[SDL_SCANCODE_W] || held_keys[SDL_SCANCODE_SPACE] || held_keys[SDL_SCANCODE_Z] || held_keys[SDL_SCANCODE_J];
    }

    state->Tick();
}

void World::Render()
{
    state->Render();
}
