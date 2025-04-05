#include "sampler.h"

#include "gpu/device.h"

#include <fmt/format.h>
#include <SDL3/SDL_gpu.h>

#include <stdexcept>
#include <utility>

namespace em::Gpu
{
    Sampler::Sampler(Device &device, const Params &params)
        : Sampler() // Ensure cleanup on throw.
    {
        SDL_GPUSamplerCreateInfo sdl_params{};

        sdl_params.min_filter = SDL_GPUFilter(params.filter_min);
        sdl_params.mag_filter = SDL_GPUFilter(params.filter_mag);

        sdl_params.address_mode_u = SDL_GPUSamplerAddressMode(params.wrap.x);
        sdl_params.address_mode_v = SDL_GPUSamplerAddressMode(params.wrap.y);
        sdl_params.address_mode_w = SDL_GPUSamplerAddressMode(params.wrap.z);

        if (params.mipmap)
        {
            // Mipmap filter uses a different enum in SDL, so we have to map here.
            sdl_params.mipmap_mode = params.mipmap->filter == Filter::nearest ? SDL_GPU_SAMPLERMIPMAPMODE_NEAREST : SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;

            sdl_params.mip_lod_bias = params.mipmap->lod_bias;
            sdl_params.min_lod = params.mipmap->lod_min;
            sdl_params.max_lod = params.mipmap->lod_max;
        }
        else
        {
            // Hopefully this (and leaving other parameters as zeroes) should disable mipmap stuff.
            // SDL has no dedicated bool for it.
            sdl_params.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        }


        if (params.compare_mode)
        {
            sdl_params.enable_compare = true;
            sdl_params.compare_op = *params.compare_mode;
        }

        if (params.anisotropy_max)
        {
            sdl_params.enable_anisotropy = true;
            sdl_params.max_anisotropy = *params.anisotropy_max;
        }

        state.device = device.Handle();
        state.sampler = SDL_CreateGPUSampler(device.Handle(), &sdl_params);
        if (!state.sampler)
            throw std::runtime_error(fmt::format("Unable to create a GPU sampler: {}", SDL_GetError()));
    }

    Sampler::Sampler(Sampler &&other) noexcept
        : state(std::move(other.state))
    {
        other.state = {};
    }

    Sampler &Sampler::operator=(Sampler other) noexcept
    {
        std::swap(state, other.state);
        return *this;
    }

    Sampler::~Sampler()
    {
        if (state.sampler)
            SDL_ReleaseGPUSampler(state.device, state.sampler);
    }
}
