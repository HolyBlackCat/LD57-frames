#include "render_pass.h"

#include "em/meta/overload.h"
#include "gpu/buffer.h"
#include "gpu/command_buffer.h"
#include "gpu/pipeline.h"
#include "gpu/sampler.h"
#include "gpu/texture.h"

#include <fmt/format.h>
#include <SDL3/SDL_gpu.h>

#include <stdexcept>
#include <utility>

namespace em::Gpu
{
    RenderPass::RenderPass(CommandBuffer &command_buffer, const Params &params)
        : RenderPass() // Ensure cleanup on throw.
    {
        std::vector<SDL_GPUColorTargetInfo> sdl_color_targets;
        sdl_color_targets.reserve(params.color_targets.size());

        for (const ColorTarget &target : params.color_targets)
        {
            SDL_GPUColorTargetInfo &sdl_target = sdl_color_targets.emplace_back();

            sdl_target.texture              = target.texture.texture->Handle();
            sdl_target.layer_or_depth_plane = target.texture.layer;
            sdl_target.mip_level            = target.texture.mipmap_level;
            sdl_target.cycle                = target.texture.cycle;

            std::visit(Meta::Overload{
                [&](const ColorClear &color)
                {
                    sdl_target.load_op = SDL_GPU_LOADOP_CLEAR;
                    sdl_target.clear_color.r = color.color.r();
                    sdl_target.clear_color.g = color.color.g();
                    sdl_target.clear_color.b = color.color.b();
                    sdl_target.clear_color.a = color.color.a();
                },
                [&](const ColorLoad &)
                {
                    sdl_target.load_op = SDL_GPU_LOADOP_LOAD;
                },
                [&](const ColorDontCare &)
                {
                    sdl_target.load_op = SDL_GPU_LOADOP_DONT_CARE;
                },
            }, target.initial_contents);

            if (target.multisample_resolved_texture)
            {
                sdl_target.store_op = target.store_output ? SDL_GPU_STOREOP_RESOLVE_AND_STORE : SDL_GPU_STOREOP_RESOLVE;
                sdl_target.resolve_texture       = target.multisample_resolved_texture->texture->Handle();
                sdl_target.resolve_layer         = target.multisample_resolved_texture->layer;
                sdl_target.resolve_mip_level     = target.multisample_resolved_texture->mipmap_level;
                sdl_target.cycle_resolve_texture = target.multisample_resolved_texture->cycle;
            }
            else
            {
                sdl_target.store_op = target.store_output ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE;
            }
        }

        SDL_GPUDepthStencilTargetInfo sdl_depth_stencil;
        if (params.depth_stencil_target)
        {
            sdl_depth_stencil.texture = params.depth_stencil_target->texture->Handle();

            std::visit(Meta::Overload{
                [&](const DepthClear &depth)
                {
                    sdl_depth_stencil.load_op = SDL_GPU_LOADOP_CLEAR;
                    sdl_depth_stencil.clear_depth = depth.value;
                },
                [&](const DepthLoad &)
                {
                    sdl_depth_stencil.load_op = SDL_GPU_LOADOP_LOAD;
                },
                [&](const DepthDontCare &)
                {
                    sdl_depth_stencil.load_op = SDL_GPU_LOADOP_DONT_CARE;
                },
            }, params.depth_stencil_target->depth.initial_contents);

            // SDL docs say multisample resolving isn't supported for depth/stencil, so we don't need anything else here.
            sdl_depth_stencil.store_op = params.depth_stencil_target->depth.store_output ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE;


            std::visit(Meta::Overload{
                [&](const StencilClear &stencil)
                {
                    sdl_depth_stencil.stencil_load_op = SDL_GPU_LOADOP_CLEAR;
                    sdl_depth_stencil.clear_stencil = stencil.value;
                },
                [&](const StencilLoad &)
                {
                    sdl_depth_stencil.stencil_load_op = SDL_GPU_LOADOP_LOAD;
                },
                [&](const StencilDontCare &)
                {
                    sdl_depth_stencil.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
                },
            }, params.depth_stencil_target->stencil.initial_contents);

            // SDL docs say multisample resolving isn't supported for depth/stencil, so we don't need anything else here.
            sdl_depth_stencil.stencil_store_op = params.depth_stencil_target->stencil.store_output ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE;


            sdl_depth_stencil.cycle = params.depth_stencil_target->cycle;
        }

        state.pass = SDL_BeginGPURenderPass(command_buffer.Handle(), sdl_color_targets.data(), std::uint32_t(sdl_color_targets.size()), params.depth_stencil_target ? &sdl_depth_stencil : nullptr);
        if (!state.pass)
            throw std::runtime_error(fmt::format("Unable to begin a GPU render pass: {}", SDL_GetError()));
    }

    RenderPass::RenderPass(RenderPass &&other) noexcept
        : state(std::move(other.state))
    {
        other.state = {};
    }

    RenderPass &RenderPass::operator=(RenderPass other) noexcept
    {
        std::swap(state, other.state);
        return *this;
    }

    RenderPass::~RenderPass()
    {
        if (state.pass)
            SDL_EndGPURenderPass(state.pass);
    }

    void RenderPass::SetViewport(const Viewport &viewport)
    {
        SDL_GPUViewport sdl_viewport{
            .x = viewport.pos.x,
            .y = viewport.pos.y,
            .w = viewport.size.x,
            .h = viewport.size.y,
            .min_depth = viewport.min_depth,
            .max_depth = viewport.max_depth,
        };
        // This can't fail.
        SDL_SetGPUViewport(state.pass, &sdl_viewport);
    }

    void RenderPass::BindPipeline(Pipeline &pipeline)
    {
        // This can't fail.
        SDL_BindGPUGraphicsPipeline(state.pass, pipeline.Handle());
    }

    void RenderPass::BindVertexBuffers(std::span<const VertexBuffer> buffers, std::uint32_t first_slot)
    {
        std::vector<SDL_GPUBufferBinding> sdl_buffers;
        sdl_buffers.reserve(buffers.size());

        for (const VertexBuffer &buffer : buffers)
        {
            sdl_buffers.push_back({
                .buffer = buffer.buffer->Handle(),
                .offset = buffer.byte_offset,
            });
        }

        // This can't fail.
        SDL_BindGPUVertexBuffers(state.pass, first_slot, sdl_buffers.data(), std::uint32_t(sdl_buffers.size()));
    }

    void RenderPass::BindTextures(std::span<const TextureAndSampler> textures, ShaderStage shader_stage, std::uint32_t first_slot)
    {
        std::vector<SDL_GPUTextureSamplerBinding> sdl_textures;
        sdl_textures.reserve(textures.size());

        for (const TextureAndSampler &texture : textures)
        {
            sdl_textures.push_back({
                .texture = texture.texture->Handle(),
                .sampler = texture.sampler->Handle(),
            });
        }

        // Those functions can't fail.
        if (shader_stage == ShaderStage::vertex)
            SDL_BindGPUVertexSamplers(state.pass, first_slot, sdl_textures.data(), std::uint32_t(sdl_textures.size()));
        else
            SDL_BindGPUFragmentSamplers(state.pass, first_slot, sdl_textures.data(), std::uint32_t(sdl_textures.size()));
    }

    void RenderPass::DrawPrimitivesInstanced(std::uint32_t num_vertices, std::uint32_t num_instances, std::uint32_t first_vertex, std::uint32_t first_instance)
    {
        SDL_DrawGPUPrimitives(state.pass, num_vertices, num_instances, first_vertex, first_instance);
    }
}
