#pragma once

#include "em/macros/utils/flag_enum.h"
#include "em/math/vector.h"

#include <SDL3/SDL_gpu.h>

namespace em::Gpu
{
    enum class TextureType
    {
        two_dim       = SDL_GPU_TEXTURETYPE_2D,
        two_dim_array = SDL_GPU_TEXTURETYPE_2D_ARRAY,
        three_dim     = SDL_GPU_TEXTURETYPE_3D,
        cube          = SDL_GPU_TEXTURETYPE_CUBE,
        cube_array    = SDL_GPU_TEXTURETYPE_CUBE_ARRAY,
    };

    enum class TextureUsageFlags
    {
        sampler                                 = SDL_GPU_TEXTUREUSAGE_SAMPLER, // Can be sampled in shaders.
        color_target                            = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, // Can render color data to this.
        depth_stencil_target                    = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET, // Can render depth/stencil to this.
        graphics_storage_read                   = SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ, // Can be read as storage in non-compute shaders.
        compute_storage_read                    = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ, // Can be read as storage in compute shaders.
        compute_storage_write                   = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE, // Can be written as storage in compute shaders.
        compute_storage_simultaneous_read_write = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_SIMULTANEOUS_READ_WRITE, // This isn't the same thing as `read | write`. That requires each shader to only read or only write, and this doesn't.
    };
    EM_FLAG_ENUM(TextureUsageFlags)

    enum class MultisampleMode
    {
        // The values here don't match the names, so you can't directly cast back and forth.
        _1 = SDL_GPU_SAMPLECOUNT_1,
        _2 = SDL_GPU_SAMPLECOUNT_2,
        _4 = SDL_GPU_SAMPLECOUNT_4,
        _8 = SDL_GPU_SAMPLECOUNT_8,
    };

    class Device;

    // A texture.
    class Texture
    {
        struct State
        {
            // At least for `SDL_ReleaseGPUTexture()`.
            // This isn't a `Device *` because that can be moved around and the internal handle can't.
            SDL_GPUDevice *device = nullptr;

            SDL_GPUTexture *texture = nullptr;

            // If false, the texture isn't destroyed with the object.
            bool owns_texture = false;

            // No way to query the size right now, apparently.
            // And also acquiring the swapchain texture returns the size, so we should store it ourselves even if there was a way to query it,
            //   since this is probably faster.
            ivec3 size;
        };
        State state;

      public:
        constexpr Texture() {}

        struct Params
        {
            // 2D/3D/cube/etc.
            TextureType type = TextureType::two_dim;

            // Pixel format.
            // There are a lot of formats, so we don't have a custom enum here.
            SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

            // Sampler/render target/etc.
            TextureUsageFlags usage = TextureUsageFlags::sampler;

            // Keep the third dimension as `1` for 2D textures.
            ivec3 size;

            // Mipmaps?
            int num_mipmap_levels = 1;

            // Multisample render target?
            MultisampleMode multisample_mode = MultisampleMode::_1;
        };

        Texture(Device &device, const Params &params);

        struct ViewExternalHandle {explicit ViewExternalHandle() = default;};
        // Put an existing handle into a texture, and don't free it when destroyed. Need this for swapchain textures.
        // Returns a null texture if `handle` is null.
        Texture(ViewExternalHandle, SDL_GPUDevice *device, SDL_GPUTexture *handle, ivec3 size);

        Texture(Texture &&other) noexcept;
        Texture &operator=(Texture other) noexcept;
        ~Texture();

        [[nodiscard]] explicit operator bool() const {return bool(state.texture);}
        [[nodiscard]] SDL_GPUTexture *Handle() {return state.texture;}

        // Returns the size. The third dimension will be 1 for 2D textures.
        [[nodiscard]] ivec3 Size() const {return state.size;}
    };
}
