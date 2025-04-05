#pragma once

#include "em/math/vector.h"

#include <SDL3/SDL_gpu.h>

#include <string_view>

namespace em
{
    namespace Gpu
    {
        class Device;
    }

    // One window. Maybe attached to a SDL GPU device, maybe not.
    class Window
    {
        struct State
        {
            SDL_Window *window = nullptr;

            // Not a `Device *` to keep the address stable.
            // Only set if a GPU device is attached.
            // This is here solely for our convenience. This lets us call `SDL_GetGPUSwapchainTextureFormat()`, for example.
            SDL_GPUDevice *gpu_device = nullptr;
        };
        State state;

      public:
        constexpr Window() {}

        struct Params
        {
            Gpu::Device *gpu_device = nullptr; // GPU device, if this window uses the SDL GPU API. Otherwise leave null.
            std::string_view name = "{name}"; // Use `{name}` and `{version}` to query the application properties passed to `em::Sdl`.
            ivec2 size = ivec2(1920, 1080) / 3;
            bool resizable = true;
        };

        Window(const Params &params);

        Window(Window &&other) noexcept;
        Window &operator=(Window other) noexcept;
        ~Window();

        [[nodiscard]] explicit operator bool() const {return bool(state.window);}
        [[nodiscard]] SDL_Window *Handle() {return state.window;}

        // The texture format this window uses for rendering.
        // Only makes sense if a GPU device is attached.
        SDL_GPUTextureFormat GetSwapchainTextureFormat() const;
    };
}
