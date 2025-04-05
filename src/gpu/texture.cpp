#include "texture.h"

#include "gpu/device.h"

#include <fmt/format.h>

#include <stdexcept>

namespace em::Gpu
{
    Texture::Texture(Device &device, const Params &params)
        : Texture() // Ensure cleanup on throw.
    {
        // Must set before creating the texture to let the destructor do its job if we throw later in this function.
        state.device = device.Handle();

        SDL_GPUTextureCreateInfo sdl_params{
            .type                 = SDL_GPUTextureType(params.type),
            .format               = params.format,
            .usage                = SDL_GPUTextureUsageFlags(params.usage),
            .width                = std::uint32_t(params.size.x),
            .height               = std::uint32_t(params.size.y),
            .layer_count_or_depth = std::uint32_t(params.size.z),
            .num_levels           = std::uint32_t(params.num_mipmap_levels),
            .sample_count         = SDL_GPUSampleCount(params.multisample_samples),
            .props                = 0, // Not exposed for now.
        };

        state.owns_texture = true;
        state.texture = SDL_CreateGPUTexture(device.Handle(), &sdl_params);
        if (!state.texture)
            throw std::runtime_error(fmt::format("Unable to create a GPU texture: {}", SDL_GetError()));
        state.size = params.size;
        state.type = params.type;
    }

    Texture::Texture(ViewExternalHandle, SDL_GPUDevice *device, SDL_GPUTexture *handle, ivec3 size, Type type)
    {
        if (handle)
        {
            state.device = device; // This better not be null.
            state.texture = handle;
            state.owns_texture = false;
            state.size = size;
            state.type = type;
        }
    }

    Texture::Texture(Texture &&other) noexcept
        : state(std::move(other.state))
    {
        other.state = {};
    }

    Texture &Texture::operator=(Texture other) noexcept
    {
        std::swap(state, other.state);
        return *this;
    }

    Texture::~Texture()
    {
        if (state.texture && state.owns_texture)
        {
            // This returns `void` and can't fail.
            // This also apparently destroys the texture lazily, when it's no longer needed, so no need to worry about synchronization issues.
            SDL_ReleaseGPUTexture(state.device, state.texture);
        }
    }
}
