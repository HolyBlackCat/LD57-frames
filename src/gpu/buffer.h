#pragma once

#include "em/macros/utils/flag_enum.h"

#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <span>

namespace em::Gpu
{
    class CopyPass;
    class Device;

    // A texture.
    class Buffer
    {
        struct State
        {
            // At least for `SDL_ReleaseGPUBuffer()`.
            // This isn't a `Device *` because that can be moved around and the SDL handle can't.
            SDL_GPUDevice *device = nullptr;

            SDL_GPUBuffer *buffer = nullptr;
        };
        State state;

      public:
        constexpr Buffer() {}

        // The usage flags.
        // Some combinations are invalid, but the SDL docs don't specify which ones.
        enum class Usage
        {
            // A regular vertex buffer.
            vertex                = SDL_GPU_BUFFERUSAGE_VERTEX,
            // An index buffer.
            index                 = SDL_GPU_BUFFERUSAGE_INDEX,

            // A buffer for indirect draw commands: https://wiki.libsdl.org/SDL3/SDL_DrawGPUPrimitivesIndirect
            // This is for drawing a bunch of different subranges of another vertex and/or index buffer,
            //   with the lists of range bounds being on GPU, in this buffer.
            indirect              = SDL_GPU_BUFFERUSAGE_INDIRECT,

            // SDL docs say:
            // 1. Having both read and and write flags is ok, but you need to be careful to avoid data races.
            // 2. Storage buffers use the "std140 layout convention", which allegedly means you must 16-byte align fvec3 and fvec4.
            //    Probably something else, you'd need to read the manual (not the SDL one, it doesn't elaborate further).

            graphics_storage_read = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
            compute_storage_read  = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ,
            compute_storage_write = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE,
        };
        EM_FLAG_ENUM_IN_CLASS(Usage)

        // Creates a buffer.
        Buffer(Device &device, std::uint32_t size, Usage usage = Usage::vertex);
        // A helper constructor that creates a buffer and immediately fills it using a temporary transfer buffer.
        Buffer(Device &device, CopyPass &pass, std::span<const unsigned char> data, Usage usage = Usage::vertex);

        Buffer(Buffer &&other) noexcept;
        Buffer &operator=(Buffer other) noexcept;
        ~Buffer();

        [[nodiscard]] explicit operator bool() const {return bool(state.buffer);}
        [[nodiscard]] SDL_GPUBuffer *Handle() {return state.buffer;}
    };
}
