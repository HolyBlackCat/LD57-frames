#include "em/macros/utils/finally.h"
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
        })
    )

    Gpu::Shader sh_v = Gpu::Shader(device, "main vert", Gpu::Shader::Stage::vertex, Filesystem::LoadedFile(fmt::format("{}assets/shaders/main.vert.spv", Filesystem::GetResourceDir())));
    Gpu::Shader sh_f = Gpu::Shader(device, "main frag", Gpu::Shader::Stage::fragment, Filesystem::LoadedFile(fmt::format("{}assets/shaders/main.frag.spv", Filesystem::GetResourceDir())));

    Gpu::Pipeline pipeline = Gpu::Pipeline(device, Gpu::Pipeline::Params{
        .shaders = {
            .vert = &sh_v,
            .frag = &sh_f,
        },
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

    Gpu::Buffer buffer = Gpu::Buffer(device, Gpu::Buffer::Params{
        .size = sizeof(fvec3) * 6,
    });

    Gpu::Texture texture;

    Gpu::Sampler sampler = Gpu::Sampler(device, Gpu::Sampler::Params{
        .filter_min = Gpu::Sampler::Filter::nearest,
        .filter_mag = Gpu::Sampler::Filter::nearest,
    });

    GameApp()
    {
        Gpu::TransferBuffer tr_buffer = Gpu::TransferBuffer(device, Gpu::TransferBuffer::Params{
            .size = sizeof(fvec3) * 6,
        });

        {
            Gpu::TransferBuffer::Mapping map = tr_buffer.Map();
            auto ptr = reinterpret_cast<fvec3 *>(map.Span().data());
            *ptr++ = fvec3(0, 0.5, 0);
            *ptr++ = fvec3(1, 0, 0);
            *ptr++ = fvec3(0.5, -0.5, 0);
            *ptr++ = fvec3(0, 1, 0);
            *ptr++ = fvec3(-0.5, -0.5, 0);
            *ptr++ = fvec3(0, 0, 1);
        }

        Gpu::CommandBuffer cmdbuf(device);
        Gpu::CopyPass pass(cmdbuf);
        tr_buffer.ApplyToBuffer(pass, buffer);

        texture = LoadImage(device, pass, "test");
    }

    App::Action Tick() override
    {
        Gpu::CommandBuffer cmdbuf(device);
        Gpu::Texture swapchain_tex = cmdbuf.WaitAndAcquireSwapchainTexture(window);

        Gpu::RenderPass rp(cmdbuf, Gpu::RenderPass::Params{
            .color_targets = {
                Gpu::RenderPass::ColorTarget{
                    .texture = {
                        .texture = &swapchain_tex,
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

        rp.BindPipeline(pipeline);
        rp.BindVertexBuffers({{Gpu::RenderPass::VertexBuffer{
            .buffer = &buffer,
        }}});
        rp.BindTextures({{{.texture = &texture, .sampler = &sampler}}});
        rp.DrawPrimitives(3);

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
