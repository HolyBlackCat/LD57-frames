#include "em/macros/utils/finally.h"
#include "em/macros/utils/lift.h"
#include "em/refl/macros/structs.h"
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

#include <iostream>
#include <memory>

using namespace em;

static constexpr ivec2 screen_size = ivec2(1920, 1080) / 4;

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

struct GameApp : App::Module
{
    EM_REFL(
        (Sdl)(sdl, AppMetadata{
            .name = "Hello world",
            .version = "0.0.1",
            // .identifier = "",
            // .author = "",
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

    ShaderPair sh_main = ShaderPair(device, "main");
    ShaderPair sh_upscale = ShaderPair(device, "upscale");

    Gpu::Pipeline pipeline_main = Gpu::Pipeline(device, Gpu::Pipeline::Params{
        .shaders = sh_main,
        .vertex_buffers = {
            {
                Gpu::Pipeline::VertexBuffer{
                    .pitch = sizeof(fvec3) * 2,
                    .attributes = {
                        Gpu::Pipeline::VertexAttribute{
                            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
                            .byte_offset_in_elem = 0,
                        },
                        Gpu::Pipeline::VertexAttribute{
                            .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
                            .byte_offset_in_elem = sizeof(fvec3),
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

    Gpu::Buffer buffer = Gpu::Buffer(device, sizeof(fvec3) * 6);

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

    Gpu::Texture texture;

    GameApp()
    {
        Gpu::CommandBuffer cmdbuf(device);
        Gpu::CopyPass pass(cmdbuf);

        texture = LoadImage(device, pass, "test");

        fvec2 upscale_triangle_verts[3] = {
            fvec2(-1, -1),
            fvec2(3, -1),
            fvec2(-1, 3),
        };
        upscale_triangle_buffer = Gpu::Buffer(device, pass, {reinterpret_cast<const unsigned char *>(upscale_triangle_verts), sizeof(upscale_triangle_verts)});
    }

    App::Action Tick() override
    {
        Gpu::CommandBuffer cmdbuf(device);
        Gpu::Texture swapchain_tex = cmdbuf.WaitAndAcquireSwapchainTexture(window);

        fvec2 skew_scale_vec2 = swapchain_tex.GetSize().to_vec2().to<float>() / screen_size;
        float scale = skew_scale_vec2.reduce(EM_FUNC(std::min));
        skew_scale_vec2 /= scale;

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


        { // Update triangle pos.
            fvec2 mouse_pos_f{};
            SDL_GetMouseState(&mouse_pos_f.x, &mouse_pos_f.y);
            ivec2 window_size{};
            SDL_GetWindowSize(window.Handle(), &window_size.x, &window_size.y);

            ivec2 mouse_pos = ((mouse_pos_f / window_size - 0.5) * screen_size).map(EM_FUNC(std::round)).to<int>();

            fvec3 arr[] = {
                fvec3(mouse_pos.x, mouse_pos.y + 50, 0),
                fvec3(1, 0, 0),
                fvec3(mouse_pos.x + 50, mouse_pos.y - 50, 0),
                fvec3(0, 1, 0),
                fvec3(mouse_pos.x - 50, mouse_pos.y - 50, 0),
                fvec3(0, 0, 1),
            };
            Gpu::CopyPass pass(cmdbuf);
            Gpu::TransferBuffer tr_buffer(device, {(const unsigned char *)arr, sizeof arr});
            tr_buffer.ApplyToBuffer(pass, buffer);
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

            if (!swapchain_tex)
            {
                std::cout << "No swapchain texture, probably the window is minimized\n";
                cmdbuf.CancelWhenDestroyed();
                return App::Action::cont; // No draw target.
            }

            fmt::println("Swapchain texture has size: [{},{},{}]", swapchain_tex.GetSize().x, swapchain_tex.GetSize().y, swapchain_tex.GetSize().z);
            fmt::println("Lowres texture has size: [{},{},{}]", upscale_triangle_texture.GetSize().x, upscale_triangle_texture.GetSize().y, upscale_triangle_texture.GetSize().z);

            rp_first.BindPipeline(pipeline_main);
            rp_first.BindVertexBuffers({{Gpu::RenderPass::VertexBuffer{
                .buffer = &buffer,
            }}});
            rp_first.BindTextures({{{.texture = &texture, .sampler = &sampler_nearest}}});
            Gpu::Shader::SetUniform(cmdbuf, Gpu::Shader::Stage::fragment, 0, (screen_size * ivec2(1,-1)).to<float>());
            rp_first.DrawPrimitives(3);
        }

        { // Upscale.
            Gpu::RenderPass rp_upscale(cmdbuf, Gpu::RenderPass::Params{
                .color_targets = {
                    Gpu::RenderPass::ColorTarget{
                        .texture = {
                            .texture = &upscale_triangle_texture_large,
                        },
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
};

std::unique_ptr<App::Module> em::Main()
{
    return std::make_unique<App::ReflectedApp<GameApp>>();
}
