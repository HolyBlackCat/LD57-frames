#include "main.h"

#include "audio/complete.h"
#include "em/macros/utils/finally.h"
#include "em/macros/utils/lift.h"
#include "em/refl/macros/structs.h"
#include "game/metronome.h"
#include "game/world.h"
#include "gpu/buffer.h"
#include "gpu/command_buffer.h"
#include "gpu/copy_pass.h"
#include "gpu/device.h"
#include "gpu/pipeline.h"
#include "gpu/render_pass.h"
#include "gpu/sampler.h"
#include "gpu/shader.h"
#include "gpu/transfer_buffer.h"
#include "mainloop/main.h"
#include "mainloop/reflected_app.h"
#include "utils/filesystem.h"
#include "window/sdl.h"
#include "window/window.h"

#include "stb_image.h"

#include <SDL3/SDL_timer.h>

#include <cstddef>
#include <memory>

using namespace em;

Audio::SourceManager audio;

Gpu::Texture LoadImage(em::Gpu::Device &device, em::Gpu::CopyPass &pass, std::string_view filename)
{
    std::string path = fmt::format("{}assets/images/{}.png", Filesystem::GetResourceDir(), filename);
    Filesystem::File file(path, "rb");
    ivec2 pixel_size;
    int num_channels = 4;
    unsigned char *stb_data = stbi_load_from_file(file.Handle(), &pixel_size.x, &pixel_size.y, nullptr, num_channels);
    if (!stb_data)
        throw std::runtime_error(fmt::format("Unable to load image `{}`.", path));
    EM_FINALLY{ stbi_image_free(stb_data); };

    std::size_t byte_size = std::size_t(pixel_size.prod() * num_channels);

    Gpu::TransferBuffer tb(device, {stb_data, byte_size});

    Gpu::Texture tex(device, Gpu::Texture::Params{
        .size = pixel_size.to_vec3(1),
    });
    tb.ApplyToTexture(pass, tex);
    return tex;
}

struct ShaderPair
{
    Gpu::Shader vert;
    Gpu::Shader frag;

    ShaderPair() {}

    ShaderPair(Gpu::Device &device, std::string_view name)
        : vert(device, fmt::format("{} (vertex)", name), Gpu::Shader::Stage::vertex, Filesystem::LoadedFile(fmt::format("{}assets/shaders/{}.vert.spv", Filesystem::GetResourceDir(), name))),
        frag(device, fmt::format("{} (fragment)", name), Gpu::Shader::Stage::fragment, Filesystem::LoadedFile(fmt::format("{}assets/shaders/{}.frag.spv", Filesystem::GetResourceDir(), name)))
    {}

    operator Gpu::Pipeline::Shaders()
    {
        return {
            .vert = &vert,
            .frag = &frag,
        };
    }
};


struct VertexAttr
{
    fvec2 pos;
    fvec4 color;
    fvec2 texcoord;
    fvec3 factors;
};


struct GameApp;
GameApp *global_app = nullptr;


struct GameApp : App::Module
{
    EM_REFL(
        (Sdl)(sdl, AppMetadata{
            .name = "LD57",
            .version = "0.0.1",
            // .identifier = "",
            .author = "HolyBlackCat",
            // .copyright = "",
            // .url = "",
        })
        (Gpu::Device)(device, Gpu::Device::Params{})
        (Window)(window, Window::Params{
            .gpu_device = &device,
            .size = screen_size * 2,
            .min_size = screen_size,
        })
    )

    Audio::Context audio_context = nullptr;

    ShaderPair sh_main = ShaderPair(device, "main");
    ShaderPair sh_upscale = ShaderPair(device, "upscale");

    Gpu::Pipeline pipeline_main = Gpu::Pipeline(device, Gpu::Pipeline::Params{
        .shaders = sh_main,
        .vertex_buffers = {
            {
                Gpu::Pipeline::VertexBuffer{
                    .pitch = sizeof(VertexAttr),
                    .attributes = {
                        Gpu::Pipeline::VertexAttribute{
                            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
                            .byte_offset_in_elem = offsetof(VertexAttr, pos),
                        },
                        Gpu::Pipeline::VertexAttribute{
                            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
                            .byte_offset_in_elem = offsetof(VertexAttr, color),
                        },
                        Gpu::Pipeline::VertexAttribute{
                            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
                            .byte_offset_in_elem = offsetof(VertexAttr, texcoord),
                        },
                        Gpu::Pipeline::VertexAttribute{
                            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
                            .byte_offset_in_elem = offsetof(VertexAttr, factors),
                        },
                    }
                }
            }
        },
        .targets = {
            .color = {
                Gpu::Pipeline::ColorTarget{
                    .texture_format = window.GetSwapchainTextureFormat(),
                    .blending = Gpu::Pipeline::Blending::Premultiplied(),
                },
            },
        },
    });

    Gpu::Pipeline pipeline_upscale = Gpu::Pipeline(device, Gpu::Pipeline::Params{
        .shaders = sh_upscale,
        .vertex_buffers = {
            {
                Gpu::Pipeline::VertexBuffer{
                    .pitch = sizeof(fvec2),
                    .attributes = {
                        Gpu::Pipeline::VertexAttribute{
                            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
                            .byte_offset_in_elem = 0,
                        },
                    }
                }
            }
        },
        .targets = {
            .color = {
                Gpu::Pipeline::ColorTarget{
                    .texture_format = window.GetSwapchainTextureFormat(),
                },
            },
        },
    });

    Gpu::Buffer upscale_triangle_buffer;
    Gpu::Texture upscale_triangle_texture = Gpu::Texture(device, Gpu::Texture::Params{
        .format = window.GetSwapchainTextureFormat(),
        .usage = Gpu::Texture::UsageFlags::sampler | Gpu::Texture::UsageFlags::color_target,
        .size = screen_size.to_vec3(1),
    });
    Gpu::Texture upscale_triangle_texture_large; // This is created in `Tick()` with the correct size.

    Gpu::Sampler sampler_nearest = Gpu::Sampler(device, Gpu::Sampler::Params{
        .filter_min = Gpu::Sampler::Filter::nearest,
        .filter_mag = Gpu::Sampler::Filter::nearest,
    });
    Gpu::Sampler sampler_linear = Gpu::Sampler(device, Gpu::Sampler::Params{
        .filter_min = Gpu::Sampler::Filter::linear,
        .filter_mag = Gpu::Sampler::Filter::linear,
    });

    // FPS counter: [
    std::uint64_t frame_counter = 0;
    std::uint64_t frame_counter_prev = 0;
    std::uint64_t tick_counter = 0;
    std::uint64_t tick_counter_prev = 0;
    std::uint64_t last_second = 0;
    int tps = 0;
    int fps = 0;
    // ]

    World world;

    Gpu::Texture main_texture;

    GameApp()
    {
        Gpu::CommandBuffer cmdbuf(device);
        Gpu::CopyPass pass(cmdbuf);

        main_texture = LoadImage(device, pass, "texture");

        fvec2 upscale_triangle_verts[3] = {
            fvec2(-1, -1),
            fvec2(3, -1),
            fvec2(-1, 3),
        };
        upscale_triangle_buffer = Gpu::Buffer(device, pass, {reinterpret_cast<const unsigned char *>(upscale_triangle_verts), sizeof(upscale_triangle_verts)});

        Audio::GlobalData::Load(Audio::mono, Audio::wav, fmt::format("{}assets/sounds/", Filesystem::GetResourceDir()));

        float audio_distance = screen_size.x * 3;
        Audio::ListenerPosition(fvec3(0, 0, -audio_distance));
        Audio::ListenerOrientation(fvec3(0,0,1), fvec3(0,-1,0));
        Audio::Source::DefaultRefDistance(audio_distance);
    }

    Metronome metronome = Metronome(60);
    std::uint64_t frame_start = std::size_t(-1);


    // Render queue.
    static constexpr int render_queue_max_verts = 3 * 1000; // Must be a multiple of three.
    Gpu::Buffer render_queue_buffer = Gpu::Buffer(device, render_queue_max_verts * sizeof(VertexAttr));
    Gpu::TransferBuffer render_queue_transfer_buffer = Gpu::TransferBuffer(device, render_queue_max_verts * sizeof(VertexAttr));
    Gpu::TransferBuffer::Mapping render_queue_mapping;
    std::size_t render_queue_num_verts = 0;

    Gpu::RenderPass *main_pass = nullptr;
    Gpu::CopyPass *queue_copy_pass = nullptr;

    void FixedTick()
    {
        world.Tick();
        tick_counter++;
    }

    App::Action Tick() override
    {
        Gpu::CommandBuffer cmdbuf(device);
        Gpu::Texture swapchain_tex = cmdbuf.WaitAndAcquireSwapchainTexture(window);

        if (!swapchain_tex)
        {
            cmdbuf.CancelWhenDestroyed();
            return App::Action::cont; // No draw target.
        }

        Gpu::CommandBuffer cmdbuf_queue_upload(device);
        Gpu::CopyPass copypass_queue_upload(cmdbuf_queue_upload);
        queue_copy_pass = &copypass_queue_upload;
        EM_FINALLY{ queue_copy_pass = nullptr; };

        // Calculate scale.
        fvec2 skew_scale_vec2 = swapchain_tex.GetSize().to_vec2().to<float>() / screen_size;
        float scale = skew_scale_vec2.reduce(EM_FUNC(std::min));
        skew_scale_vec2 /= scale;

        { // Update mouse pos.
            fvec2 mouse_pos_f{};
            SDL_GetMouseState(&mouse_pos_f.x, &mouse_pos_f.y);
            ivec2 window_size{};
            SDL_GetWindowSize(window.Handle(), &window_size.x, &window_size.y);

            ivec2 mouse_pos = ((mouse_pos_f / window_size - 0.5) * skew_scale_vec2 * screen_size).map(EM_FUNC(std::round)).to<int>();
            world.mouse_pos = mouse_pos;
        }

        { // Fixed tick.
            // Compute timings if needed.
            std::uint64_t delta = 0;
            std::uint64_t new_frame_start = Clock::Time();

            if (frame_start != std::uint64_t(-1))
                delta = new_frame_start - frame_start;

            frame_start = new_frame_start;

            while (metronome.Tick(delta))
                FixedTick();
        }

        { // Audio.
            audio.Tick();

            Audio::CheckErrors();
        }

        { // Update frame counter and FPS.
            frame_counter++;
            std::uint64_t this_second = SDL_GetTicks() / 1000;
            if (this_second != last_second)
            {
                last_second = this_second;

                fps = int(frame_counter - frame_counter_prev);
                frame_counter_prev = frame_counter;

                tps = int(tick_counter - tick_counter_prev);
                tick_counter_prev = tick_counter;

                static std::string base_title = SDL_GetWindowTitle(window.Handle());
                std::string new_title = fmt::format("{}    FPS: {}  TPS: {}  SOUNDS: {}", base_title, fps, tps, audio.ActiveSources());
                SDL_SetWindowTitle(window.Handle(), new_title.c_str());
            }
        }

        { // Recreate upscale texture (the larger one).
            int scale_int = std::max(1, (swapchain_tex.GetSize().to_vec2() / screen_size).reduce(EM_FUNC(std::min)));
            ivec2 size = screen_size * scale_int;
            if (!upscale_triangle_texture_large || upscale_triangle_texture_large.GetSize().to_vec2() != size)
            {
                upscale_triangle_texture_large = Gpu::Texture(device, Gpu::Texture::Params{
                    .format = window.GetSwapchainTextureFormat(),
                    .usage = Gpu::Texture::UsageFlags::sampler | Gpu::Texture::UsageFlags::color_target,
                    .size = size.to_vec3(1),
                });
            }
        }

        { // Primary render pass.
            Gpu::RenderPass rp_first(cmdbuf, Gpu::RenderPass::Params{
                .color_targets = {
                    Gpu::RenderPass::ColorTarget{
                        .texture = {
                            .texture = &upscale_triangle_texture,
                        },
                    },
                },
            });
            main_pass = &rp_first;
            EM_FINALLY{ main_pass = nullptr; };

            rp_first.BindPipeline(pipeline_main);

            rp_first.BindTextures({{{.texture = &main_texture, .sampler = &sampler_nearest}}});
            Gpu::Shader::SetUniform(cmdbuf, Gpu::Shader::Stage::vertex, 0, (screen_size * ivec2(1,-1)).to<float>());
            Gpu::Shader::SetUniform(cmdbuf, Gpu::Shader::Stage::fragment, 0, main_texture.GetSize().to_vec2().to<float>());

            RestartRendering();

            world.Render();

            FinishRendering();
        }

        { // Upscale.
            Gpu::RenderPass rp_upscale(cmdbuf, Gpu::RenderPass::Params{
                .color_targets = {
                    Gpu::RenderPass::ColorTarget{
                        .texture = {
                            .texture = &upscale_triangle_texture_large,
                        },
                        .initial_contents = Gpu::RenderPass::ColorDontCare{},
                    },
                },
            });

            rp_upscale.BindPipeline(pipeline_upscale);
            rp_upscale.BindVertexBuffers({{{.buffer = &upscale_triangle_buffer}}});
            rp_upscale.BindTextures({{{.texture = &upscale_triangle_texture, .sampler = &sampler_nearest}}});
            rp_upscale.DrawPrimitives(3);
        }
        { // Upscale, second pass.
            Gpu::RenderPass rp_upscale2(cmdbuf, Gpu::RenderPass::Params{
                .color_targets = {
                    Gpu::RenderPass::ColorTarget{
                        .texture = {
                            .texture = &swapchain_tex,
                        },
                        // We still clear the output, to clear the sides not covered by the viewport.
                    },
                },
            });

            rp_upscale2.BindPipeline(pipeline_upscale);
            rp_upscale2.BindVertexBuffers({{{.buffer = &upscale_triangle_buffer}}});
            rp_upscale2.BindTextures({{{.texture = &upscale_triangle_texture_large, .sampler = &sampler_linear}}});
            Gpu::RenderPass::Viewport vp{
                .pos = (swapchain_tex.GetSize().to_vec2() / 2 - screen_size / 2 * scale).map(EM_FUNC(std::round)),
                .size = (screen_size * scale).map(EM_FUNC(std::round)),
            };
            vp.pos.x = std::max(0.f, vp.pos.x);
            vp.pos.y = std::max(0.f, vp.pos.y);
            vp.size.x = std::min(float(swapchain_tex.GetSize().x), vp.pos.x + vp.size.x) - vp.pos.x;
            vp.size.y = std::min(float(swapchain_tex.GetSize().y), vp.pos.y + vp.size.y) - vp.pos.y;
            rp_upscale2.SetViewport(vp);
            rp_upscale2.DrawPrimitives(3);
        }

        return App::Action::cont;
    }

    App::Action HandleEvent(SDL_Event &e) override
    {
        if (e.type == SDL_EVENT_QUIT)
            return App::Action::exit_success;
        return App::Action::cont;
    }


    // Render queue.

    void FinishRendering()
    {
        render_queue_mapping = {};
        render_queue_transfer_buffer.ApplyToBuffer(*queue_copy_pass, render_queue_buffer);
        main_pass->BindVertexBuffers({{{.buffer = &render_queue_buffer}}});
        main_pass->DrawPrimitives(std::uint32_t(render_queue_num_verts));
        render_queue_num_verts = 0;
    }

    void RestartRendering()
    {
        render_queue_mapping = render_queue_transfer_buffer.Map();
    }

    void InsertVertex(const VertexAttr &v)
    {
        if (render_queue_num_verts >= render_queue_max_verts)
        {
            FinishRendering();
            RestartRendering();
        }

        reinterpret_cast<VertexAttr *>(render_queue_mapping.Span().data())[render_queue_num_verts++] = v;
    }
};

void DrawRect(ivec2 pos, ivec2 size, const DrawSettings &settings)
{
    VertexAttr v1{.pos = pos,                          .color = settings.color, .texcoord = settings.tex_pos,                                       .factors = settings.factors};
    VertexAttr v2{.pos = ivec2(pos.x + size.x, pos.y), .color = settings.color, .texcoord = fvec2(settings.tex_pos.x + size.x, settings.tex_pos.y), .factors = settings.factors};
    VertexAttr v3{.pos = pos + size,                   .color = settings.color, .texcoord = settings.tex_pos + size,                                .factors = settings.factors};
    VertexAttr v4{.pos = ivec2(pos.x, pos.y + size.y), .color = settings.color, .texcoord = fvec2(settings.tex_pos.x, settings.tex_pos.y + size.y), .factors = settings.factors};

    if (settings.flip_x)
    {
        std::swap(v1.texcoord, v2.texcoord);
        std::swap(v3.texcoord, v4.texcoord);
    }

    global_app->InsertVertex(v1);
    global_app->InsertVertex(v2);
    global_app->InsertVertex(v4);

    global_app->InsertVertex(v4);
    global_app->InsertVertex(v2);
    global_app->InsertVertex(v3);
}

std::unique_ptr<App::Module> em::Main()
{
    auto ret = std::make_unique<App::ReflectedApp<GameApp>>();
    global_app = &ret->underlying;
    return ret;
}
