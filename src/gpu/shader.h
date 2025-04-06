#pragma once

#include "em/zstring_view.h"

#include <cstdint>
#include <span>


typedef struct SDL_GPUDevice SDL_GPUDevice;
typedef struct SDL_GPUShader SDL_GPUShader;

namespace em::Gpu
{
    class CommandBuffer;
    class Device;

    // A texture.
    class Shader
    {
        struct State
        {
            // Need this to delete shaders.
            // This can't be `Device *` to keep the address stable.
            SDL_GPUDevice *device = nullptr;
            SDL_GPUShader *shader = nullptr;
        };
        State state;

      public:
        constexpr Shader() {}

        enum class Stage
        {
            vertex,
            fragment,
            compute,
        };

        // The name is optional.
        Shader(Device &device, zstring_view name, Stage stage, std::span<const unsigned char> spirv_binary);

        Shader(Shader &&other) noexcept;
        Shader &operator=(Shader other) noexcept;
        ~Shader();

        [[nodiscard]] explicit operator bool() const {return bool(state.shader);}
        [[nodiscard]] SDL_GPUShader *Handle() {return state.shader;}

        // Sets the uniform value.
        // SDL says if you pass a struct, you must follow std140 layout. Among other things `vec3` and `vec4` must be 16 byte aligned.
        //   I assume the alignment is relative to the start of the buffer, not necessarily in the actual RAM.
        // In vertex shaders use: `uniform(set = 1, binding = 0) uniform MyUniforms {...}`.
        // In fragment shaders use: `uniform(set = 3, binding = 0) uniform MyUniforms {...}`.
        // NOTE: SDL says it's fine to call this both outside of passes and DURING render passes (or compute passes).
        // Also SDL says you have 4 uniform slots per shader. See:  https://wiki.libsdl.org/SDL3/CategoryGPU#uniform-data
        // `glslc` rejects standalone uniforms (as opposed to struct-like `{...}`), other than samplers.
        // Note that samplers don't go through this mechanism at all, they are bound by `RenderPass::BindTextures()`.
        static void SetUniformBytes(CommandBuffer &cmdbuf, Stage stage, std::uint32_t slot, std::span<const unsigned char> span);

        // Sets the uniform value to a specific object.
        template <typename T>
        static void SetUniform(CommandBuffer &cmdbuf, Stage stage, std::uint32_t slot, const T &value)
        {
            SetUniformBytes(cmdbuf, stage, slot, {reinterpret_cast<const unsigned char *>(&value), sizeof(value)});
        }
    };
}
