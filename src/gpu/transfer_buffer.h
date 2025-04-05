#pragma once

#include "em/math/vector.h"

#include <cstdint>
#include <span>

typedef struct SDL_GPUDevice SDL_GPUDevice;
typedef struct SDL_GPUTransferBuffer SDL_GPUTransferBuffer;

namespace em::Gpu
{
    class Buffer;
    class CopyPass;
    class Device;
    class Texture;

    // A texture.
    class TransferBuffer
    {
      public:
        enum class Usage
        {
            upload,
            download,
        };

      private:
        struct State
        {
            // At least for `SDL_ReleaseGPUBuffer()`.
            // This isn't a `Device *` because that can be moved around and the SDL handle can't.
            SDL_GPUDevice *device = nullptr;

            SDL_GPUTransferBuffer *buffer = nullptr;
            std::uint32_t size = 0;

            // For our convenience.
            Usage usage{};
        };
        State state;

      public:
        constexpr TransferBuffer() {}

        struct Params
        {
            Usage usage = Usage::upload;

            std::uint32_t size = 0;
        };

        TransferBuffer(Device &device, const Params &params);

        // A helper constructor that fills this entirely from memory.
        TransferBuffer(Device &device, std::span<const unsigned char> data);

        TransferBuffer(TransferBuffer &&other) noexcept;
        TransferBuffer &operator=(TransferBuffer other) noexcept;
        ~TransferBuffer();

        [[nodiscard]] explicit operator bool() const {return bool(state.buffer);}
        [[nodiscard]] SDL_GPUTransferBuffer *Handle() {return state.buffer;}
        [[nodiscard]] std::uint32_t Size() const {return state.size;}


        class Mapping
        {
            friend TransferBuffer;

            struct State
            {
                SDL_GPUDevice *device = nullptr;
                SDL_GPUTransferBuffer *buffer = nullptr;
                std::span<unsigned char> mapped_region;
            };
            State state;

          public:
            constexpr Mapping() {}

            // The non-null construction is done by calling `TransferBuffer::Map()`, see below.

            Mapping(Mapping &&other) noexcept;
            Mapping &operator=(Mapping other) noexcept;
            ~Mapping();

            // Returns the mapped region.
            // This is const, because this class is modelled like a pointer.
            [[nodiscard]] std::span<unsigned char> Span() const {return state.mapped_region;}
        };

        // Maps the buffer into memory temporarily. Throws on failure.
        // It gets unmapped when the returned object dies.
        // Call `.Span()` on the result to get the mapped pointer.
        [[nodiscard]] Mapping Map();

        // A wrapper for `Map()` that fills the buffer from the passed pointer.
        void LoadFromMemory(const unsigned char *source);

        // Upload to a buffer or download from it (depending on constructor parameters).
        // You can assume that the upload finishes immediately, but for downloads YOU MUST WAIT for the command buffer fence.
        void ApplyToBuffer(CopyPass &pass, Buffer &target) {ApplyToBuffer(pass, 0, target, 0, Size());}
        void ApplyToBuffer(CopyPass &pass, std::uint32_t self_offset, Buffer &target, std::uint32_t target_offset, std::uint32_t size);

        struct TextureParams
        {
            // All of this can reasonably be kept defaulted.

            std::uint32_t mipmap_layer = 0;

            // When uploading to a part of the texture, this is the offset in the texture.
            uvec3 target_offset{};

            // The image size. If zero, will use the texture size (the components can be zeroed individually).
            uvec3 target_size{};

            std::uint32_t self_byte_offset = 0;

            // When the buffer holds a larger image and you want to deal with its subimage, set this to the size of the larger image. Measured in pixels.
            // Keep this zero to match the `target_size` (or if that is zero too, the texture size). The components can be zeroed individually.
            // The Y component isn't needed unless you're dealing with 3D textures (or arrays of 2D textures), I believe.
            uvec2 self_size{};
        };

        // Upload to a texture or download from it (depending on constructor parameters).
        // You can assume that the upload finishes immediately, but for downloads YOU MUST WAIT for the command buffer fence.
        void ApplyToTexture(CopyPass &pass, Texture &target) {ApplyToTexture(pass, target, {});}
        // An overload with parameters. Not using the default constructor because of the usual issue with member initializers.
        void ApplyToTexture(CopyPass &pass, Texture &target, const TextureParams &params);
    };
}
