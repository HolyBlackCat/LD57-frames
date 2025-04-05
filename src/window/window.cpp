#include "window.h"

#include "em/macros/utils/lift.h"
#include "gpu/device.h"
#include "utils/lazy_format_arg.h"

#include <fmt/format.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_video.h>

#include <stdexcept>
#include <utility>

namespace em
{
    Window::Window(const Params &params)
        : Window() // Ensure cleanup on throw.
    {
        // Strings returned by `SDL_GetAppMetadataProperty()` don't need freeing.
        std::string name = fmt::format(
            fmt::runtime(params.name),
            fmt::arg("name", LazyFormatArg(EM_EXPR(SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_NAME_STRING)))),
            fmt::arg("version", LazyFormatArg(EM_EXPR(SDL_GetAppMetadataProperty(SDL_PROP_APP_METADATA_VERSION_STRING))))
        );
        state.window = SDL_CreateWindow(name.c_str(), params.size.x, params.size.y, params.resizable * SDL_WINDOW_RESIZABLE);
        if (!state.window)
            throw std::runtime_error(fmt::format("Unable to create SDL window: {}", SDL_GetError()));

        state.gpu_device = params.gpu_device->Handle();
        if (params.gpu_device && !SDL_ClaimWindowForGPUDevice(params.gpu_device->Handle(), state.window))
            throw std::runtime_error(fmt::format("Unable to attach SDL window to the GPU device: {}", SDL_GetError()));
    }

    Window::Window(Window &&other) noexcept
        : state(std::move(other.state))
    {
        other.state = {};
    }

    Window &Window::operator=(Window other) noexcept
    {
        std::swap(state, other.state);
        return *this;
    }

    Window::~Window()
    {
        if (state.window)
            SDL_DestroyWindow(state.window);
    }

    SDL_GPUTextureFormat Window::GetSwapchainTextureFormat() const
    {
        return SDL_GetGPUSwapchainTextureFormat(state.gpu_device, state.window);
    }
}
