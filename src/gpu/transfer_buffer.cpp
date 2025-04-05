#include "transfer_buffer.h"

#include "gpu/buffer.h"
#include "gpu/copy_pass.h"
#include "gpu/device.h"
#include "gpu/texture.h"

#include <fmt/format.h>
#include <SDL3/SDL_gpu.h>

#include <cstring>
#include <stdexcept>

namespace em::Gpu
{
    TransferBuffer::TransferBuffer(Device &device, std::uint32_t size, Usage usage)
        : TransferBuffer() // Ensure cleanup on throw.
    {
        state.device = device.Handle(); // Assign this first in case we need to throw below, the destructor needs this value.
        state.size = size;
        state.usage = usage;

        SDL_GPUTransferBufferCreateInfo sdl_params{
            .usage = usage == Usage::download ? SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD : SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = size,
            .props = 0, // We don't expose this for now.
        };

        state.buffer = SDL_CreateGPUTransferBuffer(device.Handle(), &sdl_params);
        if (!state.buffer)
            throw std::runtime_error(fmt::format("Unable to create GPU transfer buffer: {}", SDL_GetError()));
    }

    TransferBuffer::TransferBuffer(Device &device, std::span<const unsigned char> data)
        : TransferBuffer(device, std::uint32_t(data.size()))
    {
        LoadFromMemory(data.data());
    }

    TransferBuffer::TransferBuffer(TransferBuffer &&other) noexcept
        : state(std::move(other.state))
    {
        other.state = {};
    }

    TransferBuffer &TransferBuffer::operator=(TransferBuffer other) noexcept
    {
        std::swap(state, other.state);
        return *this;
    }

    TransferBuffer::~TransferBuffer()
    {
        if (state.buffer)
            SDL_ReleaseGPUTransferBuffer(state.device, state.buffer);
    }

    TransferBuffer::Mapping::Mapping(Mapping &&other) noexcept
        : state(std::move(other.state))
    {
        other.state = {};
    }

    TransferBuffer::Mapping &TransferBuffer::Mapping::operator=(Mapping other) noexcept
    {
        std::swap(state, other.state);
        return *this;
    }

    TransferBuffer::Mapping::~Mapping()
    {
        if (state.buffer)
            SDL_UnmapGPUTransferBuffer(state.device, state.buffer);
    }

    // Maps the buffer into memory temporarily.
    // It gets unmapped when the returned object dies.
    TransferBuffer::Mapping TransferBuffer::Map()
    {
        Mapping ret;
        ret.state.device = state.device;
        ret.state.buffer = state.buffer;

        // Here we always cycle. Not sure why we wouldn't want to.
        void *address = SDL_MapGPUTransferBuffer(state.device, state.buffer, /*cycle:*/true);
        if (!address)
            throw std::runtime_error(fmt::format("Failed to map a GPU transfer buffer: {}", SDL_GetError()));

        ret.state.mapped_region = {reinterpret_cast<unsigned char *>(address), state.size};
        return ret;
    }

    void TransferBuffer::LoadFromMemory(const unsigned char *source)
    {
        Mapping m = Map();
        std::memcpy(m.Span().data(), source, m.Span().size());
    }

    void TransferBuffer::ApplyToBuffer(CopyPass &pass, std::uint32_t self_offset, Buffer &target, std::uint32_t target_offset, std::uint32_t size)
    {
        SDL_GPUTransferBufferLocation self_loc{
            .transfer_buffer = state.buffer,
            .offset = self_offset,
        };
        SDL_GPUBufferRegion target_loc{
            .buffer = target.Handle(),
            .offset = target_offset,
            .size = size,
        };

        // Those functions can't fail.
        if (state.usage == Usage::download)
        {
            SDL_DownloadFromGPUBuffer(pass.Handle(), &target_loc, &self_loc);
        }
        else
        {
            // Here we always cycle. Not sure why we wouldn't want to.
            SDL_UploadToGPUBuffer(pass.Handle(), &self_loc, &target_loc, /*cycle=*/true);
        }
    }

    void TransferBuffer::ApplyToTexture(CopyPass &pass, Texture &target, const TextureParams &params)
    {
        SDL_GPUTextureTransferInfo self_loc{
            .transfer_buffer = state.buffer,
            .offset = params.self_byte_offset,
            .pixels_per_row = params.self_size.x, // SDL automatically handles this being zero.
            .rows_per_layer = params.self_size.y, // Same.
        };

        // Not entirely sure about this. See: https://github.com/libsdl-org/SDL/issues/12746
        bool is_layered = Texture::TypeIsLayered(target.GetType());

        SDL_GPUTextureRegion target_loc{
            .texture = target.Handle(),
            .mip_level = params.mipmap_layer,
            .layer = is_layered ? params.target_offset.z : 0,
            .x = params.target_offset.x,
            .y = params.target_offset.y,
            .z = params.target_offset.z,
            .w = params.target_size.x ? params.target_size.x : std::uint32_t(target.GetSize().x),
            .h = params.target_size.y ? params.target_size.y : std::uint32_t(target.GetSize().y),
            .d = is_layered ? 1 : params.target_size.z ? params.target_size.z : std::uint32_t(target.GetSize().z),
        };

        // Those functions can't fail.
        if (state.usage == Usage::download)
        {
            SDL_DownloadFromGPUTexture(pass.Handle(), &target_loc, &self_loc);
        }
        else
        {
            // Here we always cycle. Not sure why we wouldn't want to.
            SDL_UploadToGPUTexture(pass.Handle(), &self_loc, &target_loc, /*cycle=*/true);
        }
    }
}
