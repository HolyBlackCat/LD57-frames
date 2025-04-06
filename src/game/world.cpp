#include "world.h"

#include "SDL3/SDL_keyboard.h"
#include "em/macros/utils/lift.h"
#include "em/macros/utils/named_loops.h"
#include "audio/global_sound_loader.h"
#include "main.h"

#include <SDL3/SDL_mouse.h>

#include <cassert>
#include <cmath>
#include <deque>
#include <random>
#include <string>

static constexpr int tile_size = 16;

static std::mt19937_64 rng(std::random_device{}());

[[nodiscard]] static int RandSign()
{
    return rng() % 2 ? 1 : -1;
}

[[nodiscard]] static float RandFloat01()
{
    static std::uniform_real_distribution<float> dist(0, 1);
    return dist(rng);
}

[[nodiscard]] static float RandFloat11()
{
    static std::uniform_real_distribution<float> dist(-1, 1);
    return dist(rng);
}

[[nodiscard]] static float RandAngle()
{
    static std::uniform_real_distribution<float> dist(-std::numbers::pi_v<float>, std::numbers::pi_v<float>);
    return dist(rng);
}


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
    Key reset;
};
static Keys keys;

// This is reset when the level is restarted. And this doesn't tick while we're in edit mode.
static int global_tick_counter_during_movement = 0;


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
            "--1--",
            "-###-",
        });
}

enum class SpawnedEntity
{
    none,
    player,
    exit,
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


    // If have an exit on this frame, this is its coordinates relative to the frame.
    std::optional<ivec2> exit_pos;


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

        { // Entities!
            if (exit_pos)
            {
                static constexpr int exit_sprite_size = 32;
                DrawRect(pos + *exit_pos - exit_sprite_size/2, ivec2(exit_sprite_size), {ivec2(global_tick_counter_during_movement / 6 % 4 * exit_sprite_size, 288), under_alpha});
            }
        }

        // Hover indicator.
        if (hovered)
            DrawRectHollow(corner_pos + 1, pixel_size - 2, 1, fvec4(1,1,1,1));
    }
};

struct Particle
{
    fvec2 pos;
    fvec2 vel;
    fvec2 acc;

    float damp = 0.01f;

    fvec4 color;

    float max_size = 0;

    int total_life = 10;
    int remaining_life = 10;

    Particle() {}
    Particle(fvec2 pos, fvec2 vel, fvec2 acc, float damp, fvec4 color, float size, int life)
        : pos(pos), vel(vel), acc(acc), damp(damp), color(color), max_size(size), total_life(life), remaining_life(life)
    {}
};




struct Level
{
    int bg_index = 0;
    std::vector<Frame> frames;
};

static const std::vector<Level> levels = {
    {
        0,
        {
            Frame(Frames::flower_island, ivec2(-50, 20), SpawnedEntity::player),
            Frame(Frames::vortex, ivec2(70, -20), SpawnedEntity::exit),
        }
    },
    {
        1,
        {
            Frame(Frames::flower_island, ivec2(-50, 20), SpawnedEntity::player),
            Frame(Frames::vortex, ivec2(70, -20), SpawnedEntity::exit),
        }
    }
};


struct Tutorial
{
    bool explaining_drag = true;
    bool explaining_move = true;
    bool explaining_reset = true;

    float drag_timer = 0;
    float move_timer = 0;
    float reset_timer = 0;

    bool dragged_at_least_once = false;
};
static Tutorial tut;


struct World::State
{
    std::vector<Frame> frames;

    std::size_t current_level_index = 0;


    float fade = 1;
    bool winning_fade_out = false;


    struct Player
    {
        bool exists = false;
        bool exists_prev = false;

        ivec2 pos;
        fvec2 vel;
        fvec2 vel_comp;
        bool on_ground = false;
        bool on_ground_prev = false;
        bool facing_left = false;
        int movement_timer = 0;
        bool holding_jump = false;

        int death_timer = 0;
    };
    Player player;


    bool movement_started = false;
    int background_movement_timer = 0;

    std::deque<Particle> particles;

    ivec2 reset_button_size = ivec2(32);
    ivec2 reset_button_pos = screen_size/2 - reset_button_size;
    bool reset_button_hovered = false;
    float reset_button_vis_timer = 0;

    State()
    {
        LoadLevelData();
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
                player.exists = true;
                player.pos = frame.pos + frame.offset_to_spawned_entity;
                break;
              case SpawnedEntity::exit:
                frame.exit_pos = frame.offset_to_spawned_entity;
                break;
            }
        }
    }

    void InitEntitiesFromFrames()
    {
        for (Frame &frame : frames)
            InitEntityFromSpecificFrame(frame);
    }

    void LoadLevelData()
    {
        frames = levels.at(current_level_index).frames;

        movement_started = false;
        player = {};

        InitEntitiesFromFrames();

        fade = 1;
        winning_fade_out = false;
    }

    void RestartLevel()
    {
        movement_started = false;
        player = {};
        InitEntitiesFromFrames();
    }

    void Tick()
    {
        static constexpr ivec2 player_hitbox[] = {
            ivec2(-4, -3),
            ivec2(-3, -3),
            ivec2(-2, -3),
            ivec2(-1, -3),
            ivec2( 0, -3),
            ivec2( 1, -3),
            ivec2( 2, -3),
            ivec2( 3, -3),

            ivec2(-4, 7),
            ivec2(-3, 7),
            ivec2(-2, 7),
            ivec2(-1, 7),
            ivec2( 0, 7),
            ivec2( 1, 7),
            ivec2( 2, 7),
            ivec2( 3, 7),

            ivec2(-4, -3),
            ivec2(-4, -2),
            ivec2(-4, -1),
            ivec2(-4,  0),
            ivec2(-4,  1),
            ivec2(-4,  2),
            ivec2(-4,  3),
            ivec2(-4,  4),
            ivec2(-4,  5),
            ivec2(-4,  6),
            ivec2(-4,  7),

            ivec2(3, -3),
            ivec2(3, -2),
            ivec2(3, -1),
            ivec2(3,  0),
            ivec2(3,  1),
            ivec2(3,  2),
            ivec2(3,  3),
            ivec2(3,  4),
            ivec2(3,  5),
            ivec2(3,  6),
            ivec2(3,  7),
        };

        { // Particles.
            // Remove dead particles.
            std::erase_if(particles, [](const Particle &part){return part.remaining_life <= 0;});

            for (Particle &part : particles)
            {
                part.pos += part.vel;
                part.vel *= 1.f - part.damp;
                part.remaining_life--;
            }
        }

        { // The reset button.
            static constexpr float
                vis_step = 0.05f;

            // Visibility timer.
            if (movement_started)
            {
                reset_button_vis_timer += vis_step;
                if (reset_button_vis_timer > 1)
                    reset_button_vis_timer = 1;
            }
            else
            {
                reset_button_vis_timer -= vis_step;
                if (reset_button_vis_timer < 0)
                    reset_button_vis_timer = 0;
            }

            // Hover check.
            if (movement_started)
            {
                reset_button_hovered =
                    mouse.pos.x >= reset_button_pos.x &&
                    mouse.pos.y >= reset_button_pos.y &&
                    mouse.pos.x < reset_button_pos.x + reset_button_size.x &&
                    mouse.pos.y < reset_button_pos.y + reset_button_size.y;

                if ((reset_button_hovered && mouse.IsPressed()) || keys.reset.IsPressed())
                {
                    player.exists = false; // Kill the player to reset.
                }
            }
            else
            {
                reset_button_hovered = false;
            }
        }


        std::size_t hovered_frame_index = std::size_t(-1);

        bool any_frame_dragged = std::any_of(frames.begin(), frames.end(), EM_MEMBER(.dragged));

        { // Resolve which frame is hovered.
            std::size_t i = frames.size();
            bool found = false;
            while (i-- > 0)
            {
                Frame &frame = frames[i];
                if (!found && !movement_started && (frame.dragged || (!reset_button_hovered && frame.WorldPixelIsInRect(mouse.pos))))
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
                    float cap = frame.dragged ? 1.7f : 1;

                    if (frame.hover_time < cap)
                    {
                        frame.hover_time += step;
                        if (frame.hover_time > cap)
                            frame.hover_time = cap;
                    }
                    else if (frame.hover_time > cap)
                    {
                        frame.hover_time -= step;
                        if (frame.hover_time < cap)
                            frame.hover_time = cap;
                    }
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


                tut.dragged_at_least_once = true;
            }

            // Finish drag.
            if ((!mouse.is_down || movement_started) && any_frame_dragged)
            {
                frames.back().dragged = false;
            }

            // Continue drag.
            if (frames.back().dragged)
            {
                frames.back().pos = mouse.pos + frames.back().drag_offset_relative_to_mouse;

                // Clamp frame position.
                ivec2 bound = screen_size / 2 - frames.back().PixelSize() / 2 - 8;
                if (frames.back().pos.x < -bound.x)
                    frames.back().pos.x = -bound.x;
                else if (frames.back().pos.x > bound.x)
                    frames.back().pos.x = bound.x;
                if (frames.back().pos.y < -bound.y)
                    frames.back().pos.y = -bound.y;
                else if (frames.back().pos.y > bound.y)
                    frames.back().pos.y = bound.y;

                // Drag the entities with the frames.
                if (!movement_started)
                    InitEntityFromSpecificFrame(frames.back());
            }
        }

        { // Update AABB overlap flags for frames.
            bool no_movement_and_found_player_frame = false;
            for (Frame &frame : frames)
            {
                frame.aabb_overlaps_player = false;
                if (!movement_started)
                    frame.player_is_under_this_frame = false;

                for (ivec2 point : player_hitbox)
                {
                    if (frame.WorldPixelIsInRect(player.pos + point))
                    {
                        frame.aabb_overlaps_player = true;

                        if (no_movement_and_found_player_frame)
                            frame.player_is_under_this_frame = true;
                        break;
                    }
                }

                if (!movement_started && frame.spawned_entity_type == SpawnedEntity::player && frame.did_spawn_entity)
                    no_movement_and_found_player_frame = true;

                // Reset the "under frame" flag if no overlap.
                if (!frame.aabb_overlaps_player)
                    frame.player_is_under_this_frame = false;
            }
        }



        { // Player to frame entity interactions. This is before player movement, this looks better.
            if (movement_started && player.exists)
            {
                for (Frame &frame : frames)
                {
                    if (frame.player_is_under_this_frame)
                        continue; // This disables the interactions.

                    // Exit?
                    if (frame.exit_pos)
                    {
                        static constexpr ivec2 exit_hitbox_size(4);

                        ivec2 exit_world_pos = frame.pos + *frame.exit_pos;

                        ivec2 dist = (exit_world_pos - player.pos).map(EM_FUNC(std::abs));
                        if (dist.x < exit_hitbox_size.x && dist.y < exit_hitbox_size.y)
                        {
                            frame.exit_pos = {};
                            audio.Play("win"_sound, exit_world_pos, 1, RandFloat11() * 0.2f);
                            player.exists = false;
                            winning_fade_out = true;
                        }
                    }
                }
            }
        }


        // Player.
        if (player.exists)
        {
            static constexpr float
                walk_speed = 1.5f,
                walk_acc = 0.4f,
                walk_dec = 0.4f,
                gravity = 0.14f,
                gravity_lowjump = 0.24f,
                max_fall_speed = 4
                ;

            int hc = keys.right.is_down - keys.left.is_down;

            // Horizontal control.
            if (hc)
            {
                if (!movement_started)
                    audio.Play("start_moving"_sound, player.pos, 1, RandFloat11() * 0.2f);
                movement_started = true;

                player.facing_left = hc < 0;

                player.vel.x += hc * walk_acc;
                if (std::abs(player.vel.x) > walk_speed)
                    player.vel.x = player.vel.x > 0 ? walk_speed : -walk_speed;
            }
            else
            {
                bool flip = player.vel.x < 0;
                if (flip)
                    player.vel.x = -player.vel.x;

                if (player.vel.x > walk_dec)
                    player.vel.x -= walk_dec;
                else
                    player.vel.x = 0;

                if (flip)
                    player.vel.x = -player.vel.x;
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
                            int r = frame.QueryWorldPixel(player.pos + point + offset);

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

            player.on_ground_prev = player.on_ground;
            player.on_ground = SolidAtOffset(ivec2(0, 1), false);

            if (player.on_ground && !player.on_ground_prev && movement_started)
            {
                audio.Play("landing"_sound, player.pos, 1, RandFloat11() * 0.3f);

                for (int i = 0; i < 8; i++)
                {
                    particles.push_back(Particle(
                        player.pos + ivec2(0,8) + fvec2(RandSign() * (2.f + 1.2f * RandFloat01()), RandFloat11()),
                        fvec2(RandFloat11() * 0.7f, RandFloat01() * -0.14f),
                        fvec2(0,-0.01f),
                        0.01f,
                        fvec3(0.7f + RandFloat01() * 0.2f).to_vec4(0.7f),
                        3,
                        30
                    ));
                }
            }


            // Jumping.
            if (player.on_ground)
            {
                if (keys.jump.IsPressed())
                {
                    movement_started = true;

                    player.holding_jump = true;

                    player.vel.y = -3;
                    player.vel_comp.y = 0;

                    audio.Play("jump"_sound, player.pos, 1, RandFloat11() * 0.3f);

                    for (int i = 0; i < 4; i++)
                    {
                        particles.push_back(Particle(
                            player.pos + ivec2(0,7) + fvec2(RandFloat11() * 4, RandFloat01()),
                            fvec2(RandFloat11() * 0.2f, RandFloat01() * -0.48f),
                            fvec2(0,-0.01f),
                            0.01f,
                            fvec3(0.7f + RandFloat01() * 0.2f).to_vec4(0.7f),
                            3,
                            30
                        ));
                    }
                }
                else
                {
                    player.holding_jump = false;

                    if (player.vel.y > 0)
                    {
                        player.vel.y = 0;
                        if (player.vel_comp.y > 0)
                            player.vel_comp.y = 0;
                    }
                }
            }
            else
            {
                if (!keys.jump.is_down || player.vel.y > 0)
                    player.holding_jump = false;

                if (movement_started)
                {
                    player.vel.y += player.holding_jump ? gravity : gravity_lowjump;
                    if (player.vel.y > max_fall_speed)
                        player.vel.y = max_fall_speed;
                }
            }


            { // Update position.
                fvec2 vel_with_comp = player.vel + player.vel_comp;
                ivec2 int_vel = vel_with_comp.map(EM_FUNC(std::round)).to<int>();
                player.vel_comp = vel_with_comp - int_vel;
                player.vel_comp *= 0.98f;

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
                            if (int_vel[vert] * player.vel[vert] > 0)
                            {
                                player.vel[vert] = 0;
                                if (int_vel[vert] * player.vel_comp[vert] > 0)
                                    player.vel_comp[vert] = 0;
                            }
                            int_vel[vert] = 0;
                        }
                        else
                        {
                            int_vel -= offset;
                            player.pos += offset;

                            if (!vert)
                                moved_x = true;
                        }
                    }
                }

                player.pos += int_vel;

                if (moved_x)
                    player.movement_timer++;
                else
                    player.movement_timer = 0;
            }


            // Hide tutorial messages once we start moving.
            if (movement_started)
            {
                tut.explaining_move = false;
                tut.explaining_drag = false; // Let's do this here.
            }
        }

        // Player death conditions.
        if (player.exists)
        {
            // Falling out of bounds.
            // Jumping above the bounds is allowed though.
            if (player.exists && (player.pos.x <= -screen_size.x / 2 || player.pos.x > screen_size.x / 2 || player.pos.y > screen_size.y / 2))
            {
                player.exists = false;
            }
        }

        // Player death.
        if (!player.exists && player.exists_prev && !winning_fade_out)
        {
            audio.Play("death"_sound, player.pos, 1, RandFloat11() * 0.1f);

            // Do this on any death, not only on clicking reset.
            tut.explaining_reset = false;

            for (int i = 0; i < 64; i++)
            {
                float a1 = RandAngle();
                float a2 = RandAngle();

                particles.push_back(Particle(
                    player.pos + fvec2(std::cos(a1), std::sin(a1)) * (RandFloat01() * 6),
                    fvec2(std::cos(a2), std::sin(a2)) * (RandFloat01() * 2.f),
                    fvec2(),
                    0.01f,
                    fvec3(0.6f + RandFloat01() * 0.4f).to_vec4(0.5f + RandFloat01() * 0.5f),
                    4,
                    90
                ));
            }
        }
        player.exists_prev = player.exists;

        { // Restarting on death. Also switching to the next level on win.
            if (!player.exists)
            {
                player.death_timer++;
                if (player.death_timer > 45)
                {
                    if (winning_fade_out)
                    {
                        current_level_index++;
                        if (current_level_index >= levels.size())
                            std::exit(0); // Oh well.

                        LoadLevelData();
                    }
                    else
                    {
                        audio.Play("respawn"_sound, player.pos, 1, RandFloat11() * 0.2f);
                        RestartLevel();

                        for (int i = 0; i < 16; i++)
                        {
                            float a1 = RandAngle();

                            particles.push_back(Particle(
                                player.pos + fvec2(std::cos(a1), std::sin(a1)) * (3 + RandFloat01()),
                                fvec2(std::cos(a1), std::sin(a1)) * (1),
                                fvec2(),
                                0.05f,
                                fvec3(0.7f + RandFloat01() * 0.2f).to_vec4(1),
                                3,
                                20
                            ));
                        }
                    }
                }
            }
        }


        { // Fade.
            static constexpr float fade_step = 0.03f;

            if (winning_fade_out)
            {
                fade += fade_step;
                if (fade > 1)
                    fade = 1;
            }
            else
            {
                fade -= fade_step;
                if (fade < 0)
                    fade = 0;
            }
        }


        { // Tutorial texts.
            constexpr float step = 0.005f;

            if (tut.explaining_drag)
            {
                tut.drag_timer += step;
                if (tut.drag_timer > 1)
                    tut.drag_timer = 1;
            }
            else
            {
                tut.drag_timer -= step;
                if (tut.drag_timer < 0)
                    tut.drag_timer = 0;
            }

            if (tut.explaining_move && tut.dragged_at_least_once)
            {
                tut.move_timer += step;
                if (tut.move_timer > 1)
                    tut.move_timer = 1;
            }
            else
            {
                tut.move_timer -= step;
                if (tut.move_timer < 0)
                    tut.move_timer = 0;
            }

            if (tut.explaining_reset && movement_started)
            {
                tut.reset_timer += step;
                if (tut.reset_timer > 1)
                    tut.reset_timer = 1;
            }
            else
            {
                tut.reset_timer -= step;
                if (tut.reset_timer < 0)
                    tut.reset_timer = 0;
            }
        }

        { // Hints.
            // If the player keeps clicking after starting to move, show them the tutorial again.
            if (movement_started && !reset_button_hovered && mouse.IsPressed())
                tut.explaining_reset = true;
        }

        { // Background movement.
            if (movement_started)
                background_movement_timer++;
        }


        { // Global tick counter.
            if (movement_started)
                global_tick_counter_during_movement++;
            else
                global_tick_counter_during_movement = 0;
        }
    }

    void Render() const
    {
        { // Background.
            static constexpr ivec2 bg_size(128);
            ivec2 count = (screen_size + bg_size - 1) / bg_size;
            for (int y = 0; y < count.y; y++)
            for (int x = -1; x < count.x; x++)
            {
                DrawRect(ivec2(x, y) * bg_size - screen_size / 2 + ivec2(background_movement_timer / 2 % bg_size.x, 0), bg_size, ivec2(bg_size.x * levels[current_level_index].bg_index, 0));
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

        { // Frame borders that are visible through other frames. Only doing this for non-transparent frames.
            for (std::size_t i = 0; i < frame_index; i++)
            {
                const Frame &frame = frames[i];
                DrawRectHollow(frame.TopLeftCorner(), frame.PixelSize(), 1, fvec4(0,0,0,0.06f));
            }
        }

        // Player.
        if (player.exists)
        {
            static constexpr int player_sprite_size = 16;

            int pl_state = 0;
            int pl_frame = 0;
            if (player.on_ground)
            {
                if (player.movement_timer > 0)
                {
                    pl_state = 1;
                    pl_frame = player.movement_timer / 3 % 4;
                }
            }
            else
            {
                pl_state = 2;
                if (player.vel.y < -1)
                    pl_frame = 0;
                else if (player.vel.y < -0.5f)
                    pl_frame = 1;
                else if (player.vel.y < 0)
                    pl_frame = 2;
                else if (player.vel.y < 0.5f)
                    pl_frame = 3;
                else
                    pl_frame = 4;
            }

            DrawRect(player.pos - player_sprite_size / 2 + ivec2(0,2), ivec2(player_sprite_size), {ivec2(0, 240) + ivec2(pl_frame, pl_state) * player_sprite_size, 1, 1, player.facing_left});

        }

        { // Particles.
            for (const Particle &part : particles)
            {
                int size = (int)std::round(part.max_size * part.remaining_life / part.total_life);

                ivec2 corner = (part.pos - size / 2).map(EM_FUNC(std::round)).to<int>();

                DrawRect(corner, ivec2(size), part.color);
            }
        }

        // Frames above the player.
        for (; frame_index < frames.size(); frame_index++)
        {
            frames[frame_index].Render();
        }


        { // The tutorial texts.
            static constexpr ivec2 text_size(192, 16);

            auto MapTimer = [&](float t)
            {
                return std::clamp(t * 3 - 1, 0.f, 1.f);
            };

            if (float t = MapTimer(tut.drag_timer); t > 0.001f)
                DrawRect(ivec2(-text_size.x / 2, screen_size.y / 2 - text_size.y) - ivec2(0, text_size.y * 2), text_size, {ivec2(0, 352 + text_size.y * 0), t});
            if (float t = MapTimer(tut.move_timer); t > 0.001f)
                DrawRect(ivec2(-text_size.x / 2, screen_size.y / 2 - text_size.y) - ivec2(0, text_size.y * 1), text_size, {ivec2(0, 352 + text_size.y * 1), t});
            if (float t = MapTimer(tut.reset_timer); t > 0.001f)
                DrawRect(ivec2(-text_size.x / 2, screen_size.y / 2 - text_size.y) - ivec2(0, text_size.y * 0), text_size, {ivec2(0, 352 + text_size.y * 2), t});
        }

        // The reset button.
        if (movement_started)
        {
            DrawRect(reset_button_pos, reset_button_size, {ivec2(reset_button_size.x * reset_button_hovered, 320), reset_button_vis_timer});
        }

        { // Fade.
            if (fade > 0.001f)
                DrawRect(-screen_size / 2, screen_size, fvec4(0,0,0,fade));
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
        keys.reset.is_down_prev = keys.reset.is_down;

        keys.left .is_down = held_keys[SDL_SCANCODE_LEFT ] || held_keys[SDL_SCANCODE_A];
        keys.right.is_down = held_keys[SDL_SCANCODE_RIGHT] || held_keys[SDL_SCANCODE_D];
        keys.jump .is_down = held_keys[SDL_SCANCODE_UP   ] || held_keys[SDL_SCANCODE_W] || held_keys[SDL_SCANCODE_SPACE] || held_keys[SDL_SCANCODE_Z] || held_keys[SDL_SCANCODE_J];

        keys.reset.is_down = held_keys[SDL_SCANCODE_R] || held_keys[SDL_SCANCODE_ESCAPE];
    }

    state->Tick();
}

void World::Render()
{
    state->Render();
}
