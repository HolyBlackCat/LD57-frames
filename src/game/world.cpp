#include "world.h"

#include "em/macros/utils/lift.h"
#include "em/macros/utils/named_loops.h"
#include "main.h"

#include <SDL3/SDL_mouse.h>

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

    [[nodiscard]] bool PixelIsInRect(ivec2 pixel) const
    {
        ivec2 a = TopLeftCorner();
        ivec2 b = a + PixelSize();
        return pixel.x >= a.x && pixel.y >= a.y && pixel.x < b.x && pixel.y < b.y;
    }

    void Render() const
    {
        ivec2 corner_pos = TopLeftCorner();
        ivec2 pixel_size = PixelSize();

        // Shadow.
        DrawRectAbs(
            corner_pos - ivec2(-1) - (hover_time * ivec2(-1,-1)).map(EM_FUNC(std::round)).to<int>(),
            corner_pos + pixel_size + ivec2(2,2) + (hover_time * ivec2(1,3)).map(EM_FUNC(std::round)).to<int>(),
            fvec4(0,0,0,0.5f)
        );

        // The image.
        DrawRect(corner_pos, pixel_size, {ivec2(0, 128) + tile_size * type->tex_pos, 1, 1});

        // The frame.
        DrawRectHollow(corner_pos, pixel_size, 1, fvec4(0,0,0,1));

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
        std::size_t hovered_frame_index = std::size_t(-1);

        bool any_frame_dragged = std::any_of(frames.begin(), frames.end(), EM_MEMBER(.dragged));

        { // Resolve which frame is hovered.
            std::size_t i = frames.size();
            bool found = false;
            while (i-- > 0)
            {
                Frame &frame = frames[i];
                if (!found && (frame.dragged || frame.PixelIsInRect(mouse.pos)))
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
    }

    void Render()
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
        for (const Frame &frame : frames)
        {
            frame.Render();
        }

        // Player.
        if (player_exists)
        {
            static constexpr int player_sprite_size = 16;

            DrawRect(player_pos - player_sprite_size / 2, ivec2(player_sprite_size), {ivec2(0, 240)});
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
    mouse.pos = mouse_pos;

    mouse.is_down_prev = mouse.is_down;

    SDL_MouseButtonFlags sdl_mouse_flags = SDL_GetMouseState(nullptr, nullptr);
    mouse.is_down = bool(sdl_mouse_flags & SDL_BUTTON_LEFT);

    state->Tick();
}

void World::Render()
{
    state->Render();
}
