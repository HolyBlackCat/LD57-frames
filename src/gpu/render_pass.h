#pragma once

#include "em/math/vector.h"

#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>
#include <variant>

namespace em::Gpu
{
    class Buffer;
    class CommandBuffer;
    class Pipeline;
    class Sampler;
    class Shader;
    class Texture;

    class RenderPass
    {
        struct State
        {
            SDL_GPURenderPass *pass = nullptr;
        };
        State state;

      public:
        constexpr RenderPass() {}

        // Clear the contents.
        struct ColorClear
        {
            // I'd like to set this to `fvec4(0,0,0,1)` right here,
            //   but then `ColorInitialContents initial_contents;` doesn't find the default constructor of this type unless we make it user-declared,
            //   which makes us lose aggregate-ness, which isn't what I want. So we keep this default-constructed, and provide a default color below.
            fvec4 color;
        };
        // Load the existing texture contents and don't clear them.
        struct ColorLoad {};
        // Neither load nor clear. The target contents will start in an undefined state.
        struct ColorDontCare {};

        using ColorInitialContents = std::variant<ColorClear, ColorLoad, ColorDontCare>;

        struct TextureTarget
        {
            Texture *texture = nullptr;

            // A layer for texture arrays or 3D textures.
            std::uint32_t layer = 0;
            // A mipmap level to write to.
            std::uint32_t mipmap_level = 0;

            // Normally we don't expose the cycle flag, but just in case...
            bool cycle = true;
        };

        struct ColorTarget
        {
            // The target texture.
            TextureTarget texture;

            // The initial contents of this texture.
            ColorInitialContents initial_contents = ColorClear{fvec4(0,0,0,1)};

            // If false the output will be discarded. If true it will be written to the texture.
            bool store_output = true;

            // You can optionally set this if `texture` is a multisample texture.
            // Then we'll also produce NON-multisample results and write them here.
            // Then you can also set `store_output = false` if you don't need the multisample results, and only need the non-multisample ones.
            //
            // Adding `{}` to prevent Clang from warning when omitting an initializer for this in designated initialization.
            std::optional<TextureTarget> multisample_resolved_texture{};
        };

        struct DepthClear
        {
            // I'd like to set this to `1` right here,
            //   but then `DepthInitialContents initial_contents;` doesn't find the default constructor of this type unless we make it user-declared,
            //   which makes us lose aggregate-ness, which isn't what I want. So we keep this default-constructed, and provide a default color below.
            float value;
        };
        struct DepthLoad {};
        struct DepthDontCare {};

        using DepthInitialContents = std::variant<DepthClear, DepthLoad, DepthDontCare>;

        struct DepthTarget
        {
            // Should we store the output?
            // Docs recommend false for oneshot depth textures.
            bool store_output = false;

            DepthInitialContents initial_contents = DepthClear{1};
        };

        struct StencilClear
        {
            // I'd like to set this to some value right here,
            //   but then `StencilInitialContents initial_contents;` doesn't find the default constructor of this type unless we make it user-declared,
            //   which makes us lose aggregate-ness, which isn't what I want. So we keep this default-constructed, and provide a default color below.
            std::uint8_t value;
        };
        struct StencilLoad {};
        struct StencilDontCare {};

        using StencilInitialContents = std::variant<StencilClear, StencilLoad, StencilDontCare>;

        struct StencilTarget
        {
            // Should we store the output?
            // Docs recommend false for oneshot stencil textures.
            bool store_output = false;

            StencilInitialContents initial_contents = StencilClear{0};
        };

        struct DepthStencil
        {
            Texture *texture = nullptr;

            DepthTarget depth;
            StencilTarget stencil;

            // Normally we don't expose the cycle flag, but just in case...
            bool cycle = true;

            // SDL docs say that depth/stencil don't support multisample resolving, see: https://wiki.libsdl.org/SDL3/SDL_GPUDepthStencilTargetInfo
            // So there's no `multisample_resolved_texture` here.
        };

        struct Params
        {
            std::vector<ColorTarget> color_targets;

            // Adding `{}` to prevent Clang from warning when omitting an initializer for this in designated initialization.
            std::optional<DepthStencil> depth_stencil_target{};
        };

        RenderPass(CommandBuffer &command_buffer, const Params &params);

        RenderPass(RenderPass &&other) noexcept;
        RenderPass &operator=(RenderPass other) noexcept;
        ~RenderPass();

        [[nodiscard]] explicit operator bool() const {return bool(state.pass);}
        [[nodiscard]] SDL_GPURenderPass *Handle() {return state.pass;}


        void BindPipeline(Pipeline &pipeline);


        struct VertexBuffer
        {
            Buffer *buffer = nullptr;

            std::uint32_t byte_offset = 0;
        };

        // Select what vertex buffer to use.
        void BindVertexBuffers(std::span<const VertexBuffer> buffers, std::uint32_t first_slot = 0);


        struct TextureAndSampler
        {
            Texture *texture = nullptr;
            Sampler *sampler = nullptr;
        };

        // This is separate from `Shader::Stage` because we can't list compute shaders here, because they must be used in a compute pass, not here.
        enum class ShaderStage
        {
            vertex,
            fragment,
        };

        // Select what textures to use. The slots are in a separate namespace unrelated to vertex buffers,
        //   and apparently each shader stage has its own namespace.
        // In vertex shaders use `layout(set = 0, binding = MySlotIndex) uniform sampler2D` (or other sampler types).
        // In fragment shaders use `layout(set = 2, binding = MySlotIndex) uniform sampler2D` (or other sampler types).
        // See for more details:  https://wiki.libsdl.org/SDL3/SDL_CreateGPUShader
        void BindTextures(std::span<const TextureAndSampler> textures, ShaderStage shader_stage = ShaderStage::fragment, std::uint32_t first_slot = 0);


        // Drawing:

        void DrawPrimitives(std::uint32_t num_vertices, std::uint32_t first_vertex = 0) {DrawPrimitivesInstanced(num_vertices, 1, first_vertex, 0);}
        void DrawPrimitivesInstanced(std::uint32_t num_vertices, std::uint32_t num_instances, std::uint32_t first_vertex = 0, std::uint32_t first_instance = 0);
    };
}
