#pragma once

#include "em/math/vector.h"
#include "gpu/multisample.h"

#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace em::Gpu
{
    class Device;
    class Shader;

    // This stores one vertex and one fragment shader.
    //
    // This also descibes how many vertex attributes there are, what types, and how many input vertex buffers there are, and the layout of the attributes
    //   in the buffers (you can have one buffer with all attributes, or split them between different buffers, possibly still more than one per buffer).
    // For each attribute it specifies the location that must match the one in the shader.
    // For each input vertex buffer it assigns an arbitrary slot index that you must match later when binding the vertex buffers to draw them.
    // Also each input buffer (not attribute) can be set to advance by one per instance (as opposed to per vertex).
    //
    // Also how many color outputs there are, and whether there's depth and/or stencil outputs and their settings.
    //
    // This also holds most of the tuning knobs: primitive type, front- and backface culling, which winding order is front or back, and multisample settings.
    class Pipeline
    {
        struct State
        {
            // This this at least to destroy the pipeline.
            // Using this instead of `Device *` to keep the address stable.
            SDL_GPUDevice *device = nullptr;
            SDL_GPUGraphicsPipeline *pipeline = nullptr;
        };
        State state;

      public:
        struct Shaders
        {
            // Both are not optional.
            Shader *vert = nullptr;
            Shader *frag = nullptr;
        };

        struct VertexAttribute
        {
            // Must match whatever the shader specifies.
            // Must be either all specified or all unspecified. If not specified, we use incremental indices.
            // Adding initializer to tell Clangd that this is optional when using designated initializers.
            std::optional<std::uint32_t> custom_location_in_shader = {};

            // How many components and their type. For integers also whether to normalize them (unsigned to 0..1, signed to -1..1).
            SDL_GPUVertexElementFormat format{};

            // Each vertex BUFFER has a repeating block of some size. This is this attribute's offset in that block.
            std::uint32_t byte_offset_in_elem = 0;
        };

        struct VertexBuffer
        {
            // The element size.
            std::uint32_t pitch = 0;

            // If true, advance the element per instance (for instanced rendering), not per vertex.
            bool per_instance = false;

            std::vector<VertexAttribute> attributes;
        };

        enum class Primitive
        {
            triangles      = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,  // Individual triangles.
            triangle_strip = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP, // Each triangle reuses two last vertices from the previous one.
            lines          = SDL_GPU_PRIMITIVETYPE_LINELIST,      // Individual lines.
            line_strip     = SDL_GPU_PRIMITIVETYPE_LINESTRIP,     // Each line reuses the end point of the previous one.
            points         = SDL_GPU_PRIMITIVETYPE_POINTLIST,     // Individual points.
        };

        enum class Culling
        {
            allow_all, // No culling.
            allow_front, // Allow front faces, cull (remove) back faces. Which faces are front is set separately.
            allow_back, // Allow back faces, cull (remove) front faces.
        };

        // This is responsible for adjusting the resulting depth.
        struct DepthBias
        {
            // The exact meaning of values is something I still need to figure out and document.
            // It seems that positive values move the things AWAY from the camera, but I've yet to test this.

            // This is a fixed number added to the depth.
            // This isn't measured in [0;1] units. When the depth buffer is integral, this is in the same units as the integer value.
            float constant_factor = 0;

            // This is multiplied by `max(abs(dx/dz),abs(dy/dz))`, but I don't know in what units this is measured.
            // This means this is multiplied by zero when the polygon is parallel to the screen plane.
            float slope_factor = 0;

            // The sum of the two parts above is clamped between 0 and this number.
            // I believe this can be negative, and the clamp still works reasonably, clamping to range `[clamp;0]`.
            float clamp = 0;
        };

        struct Rasterizer
        {
            // Don't fill triangles. SDL docs warn that "many" android phones don't have this feature and will silently disable it.
            bool wireframe = false;

            // Backface culling?
            Culling culling = Culling::allow_all;

            // if true, front faces are the clockwise ones. Otherwise the counter-clockwise ones.
            // False matches the default GL behavior.
            bool front_faces_are_clockwise = false;

            // Adjust output depth.
            // We could move this to `struct Depth`, but SDL places it in the rasterizer settings.
            //   Does it have some effect even when the depth buffer is turned off? Not sure.
            std::optional<DepthBias> depth_bias;

            // Don't render things outside of the near/far plane.
            // If this is false, they are still rendered and the out-of-range depth is clamped to fit into the depth buffer (or rather to the depth range
            //   associated with your viewport).
            // It seems that things with `w < 0` are always clipped regardless,
            //   so you don't need to manually clip the half-space behind the camera even when disabling this.
            // SDL docs warn that setting this to true has a weird interaction with shaders that write custom `gl_FragDepth`.
            //   (Yes, they don't mention `gl_FragDepth` at the time of writing, but it seems to be a documentation issue.)
            //   On D3D12 the resulting depth is always clamped. But on other APIs it is not, it stops being clamped when you set `clip = true`,
            //   and then writing out-of-range depth in the shader is UB, and can result in depth values outside of the range specified in the viewport,
            //   or even outside of the [0;1] range if your depth-buffer is floating-point, apparently.
            //   See the following message about the UB part: https://github.com/gpuweb/gpuweb/issues/2100#issuecomment-926080092
            //   And in general, see that entire thread for the discussion of how this behaves on different APIs.
            //
            // You'd think this should be in `struct Depth`, but this has an effect even when the depth testing is turned off.
            bool clip_by_depth = true;
        };

        // Multisampling settings.
        // This is a separate struct because there are more settings that they don't expose yet, that they might add later. Such as the sample mask.
        struct Multisample
        {
            // How many samples.
            MultisampleSamples samples = MultisampleSamples::_1;
        };

        struct Depth
        {
            // The condition for the depth test.
            // Since by default the positive depth direction is further away from the camera, this is usually "less".
            // Note that this is despite the Z axis being towards the camera. The depth output is flipped from that.
            SDL_GPUCompareOp depth_pass_condition = SDL_GPU_COMPAREOP_LESS;

            // Should we update the depth buffer, or only test?
            // SDL says that this can only be true if the depth testing is true, that's why this is inside an optional struct that enables
            //   depth testing. If you want to only write the depth but don't test, you'll have to set `depth_pass_condition` to `ALWAYS`.
            bool write_depth = true;
        };

        // Stencil settings that are separate for front-facing and back-facing triangles.
        struct StencilOperation
        {
            // What to do with fragments that failed the stencil test.
            SDL_GPUStencilOp on_fail_stencil = SDL_GPU_STENCILOP_KEEP;
            // What to do with fragments that passed the stencil test and then passed the depth test.
            SDL_GPUStencilOp on_pass_stencil_and_depth = SDL_GPU_STENCILOP_KEEP;
            // What to do with fragments that passed the stencil test and then FAILED the depth test.
            // Apparently stencil is checked before depth.
            SDL_GPUStencilOp on_pass_stencil_but_fail_depth = SDL_GPU_STENCILOP_KEEP;

            // What condition is used for the stencil test.
            SDL_GPUCompareOp stencil_pass_condition{};
        };

        struct Stencil
        {
            StencilOperation front_faces;
            StencilOperation back_faces;

            // Only those bits are compared by `stencil_pass_condition`, the rest are ignored.
            std::uint8_t compare_mask = 0xff;

            // Only those bits are updated by the resulting write;
            std::uint8_t write_mask = 0xff;
        };

        // The blending settings separate for color and for alpha.
        struct ChannelBlending
        {
            SDL_GPUBlendFactor source{};
            SDL_GPUBlendFactor target{};

            SDL_GPUBlendOp operation = SDL_GPU_BLENDOP_ADD;
        };

        // Combined color and alpha settings.
        struct Blending
        {
            ChannelBlending color;
            ChannelBlending alpha;
        };

        // A single color target of the rendering.
        struct ColorTarget
        {
            // The texture format. Defaults to the classical RGBA8.
            SDL_GPUTextureFormat texture_format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;

            // The blending mode.
            // Must construct this to enable blending.
            // Adding `{}` to disable Clang warning about omitting the initializer for this in designated initialization.
            std::optional<Blending> blending{};

            // Which color channels to update. All ones by default.
            bvec4 color_write_mask = bvec4(1);
        };

        struct Targets
        {
            // The list of color targets. We add one simple target by default.
            // I think SDL docs said somewhere this should be at most 4.
            std::vector<ColorTarget> color = {ColorTarget{}};

            // Set this to enable the depth/stencil target.
            // Adding `{}` to disable Clang warning about omitting the initializer for this in designated initialization.
            std::optional<SDL_GPUTextureFormat> depth_stencil_format{};
        };

        struct Params
        {
            // Mandatory parts:

            // What shaders to use.
            Shaders shaders;

            // The vertex attributes and how they are laid out in the input vertex buffers.
            std::vector<VertexBuffer> vertex_buffers;

            // Optional parts:
            // Adding `{}` on all those to make Clang not warn when omitting initializers for them.

            // What primitive to draw, triangles or something else ("triangle strip" and "line strip" also go here).
            Primitive primitive = Primitive::triangles;

            // Controls the rasterizer: backface culling, wireframe mode, and depth clipping.
            //   Also for some reason the depth bias (offset), I wonder why it's not in the depth settings.
            Rasterizer rasterizer{};

            // Multisampling settings.
            Multisample multisample{};

            // Depth test.
            // You must construct this to enable the depth test. Then also don't forget to set `targets.depth_stencil_format`.
            std::optional<Depth> depth{};

            // Stencil test.
            // You must default-construct this to enable the stencil test.
            std::optional<Stencil> stencil{};

            // The output targets.
            Targets targets{};
        };

        constexpr Pipeline() {}

        enum class Stage
        {
            vertex,
            fragment,
            compute,
        };

        Pipeline(Device &device, const Params &params);

        Pipeline(Pipeline &&other) noexcept;
        Pipeline &operator=(Pipeline other) noexcept;
        ~Pipeline();

        [[nodiscard]] explicit operator bool() const {return bool(state.pipeline);}
        [[nodiscard]] SDL_GPUGraphicsPipeline *Handle() {return state.pipeline;}
    };
}
