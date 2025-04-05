#include "buffer.h"

#include "gpu/device.h"
#include "gpu/transfer_buffer.h"

#include <fmt/format.h>

#include <stdexcept>

namespace em::Gpu
{
    Buffer::Buffer(Device &device, std::uint32_t size, Usage usage)
        : Buffer() // Ensure cleanup on throw.
    {
        state.device = device.Handle(); // Assign this first in case we need to throw below, the destructor needs this value.

        SDL_GPUBufferCreateInfo sdl_params{
            .usage = SDL_GPUBufferUsageFlags(usage),
            .size = size,
            .props = 0, // We don't expose this for now.
        };

        state.buffer = SDL_CreateGPUBuffer(device.Handle(), &sdl_params);
        if (!state.buffer)
            throw std::runtime_error(fmt::format("Unable to create GPU buffer: {}", SDL_GetError()));
    }

    Buffer::Buffer(Device &device, CopyPass &pass, std::span<const unsigned char> data, Usage usage)
        : Buffer(device, std::uint32_t(data.size()), usage)
    {
        TransferBuffer tb(device, data);
        tb.ApplyToBuffer(pass, *this);
    }

    Buffer::Buffer(Buffer &&other) noexcept
        : state(std::move(other.state))
    {
        other.state = {};
    }

    Buffer &Buffer::operator=(Buffer other) noexcept
    {
        std::swap(state, other.state);
        return *this;
    }

    Buffer::~Buffer()
    {
        if (state.buffer)
            SDL_ReleaseGPUBuffer(state.device, state.buffer);
    }
}
