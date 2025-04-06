#include "device.h"

#include "em/macros/meta/if_else.h"
#ifdef _WIN32
#include "em/macros/utils/finally.h"
#endif

#include <fmt/format.h>
#include <SDL3_shadercross/SDL_shadercross.h>
#include <SDL3/SDL_gpu.h>

#include <stdexcept>
#include <utility>


namespace em::Gpu
{
    Device::Device(const Params &params)
        : Device() // Ensure cleanup on throw.
    {
        // If `EM_SDLGPU_DEBUG` is defined to 0 or 1, uses that value. Otherwise 1 if `NDEBUG` is not defined.
        state.debug_mode_enabled = EM_IS_TRUTHY(EM_TRUTHY_OR_FALLBACK(EM_SDLGPU_DEBUG)(EM_IS_FALSEY(EM_IS_EMPTY_OR_01(NDEBUG))));
        state.device = SDL_CreateGPUDevice(SDL_ShaderCross_GetSPIRVShaderFormats(), state.debug_mode_enabled, nullptr);
        if (!state.device)
        {
            std::string initial_error = SDL_GetError();

            // Fall back to software rendering if we know how.
            // On Windows we try to use Swiftshader (a software Vulkan implementation) that's shipped with Edge (and with all other Chrome-based
            //   browsers too, but we use the version from Edge as it should always be installed).
            #ifdef _WIN32
            if (params.fallback_to_software_rendering)
            {
                std::string str = "C:\\Program Files (x86)\\Microsoft\\Edge\\Application";

                // For some reason `Program Files (x64)` is used even on fresh Windows 11 installs. Hmm.
                char **globbed_files = SDL_GlobDirectory(str.c_str(), "*", SDL_GLOB_CASEINSENSITIVE, nullptr);
                EM_FINALLY{ SDL_free(globbed_files); }; // Works even if the pointer is null.

                if (globbed_files && *globbed_files)
                {
                    str += '\\';
                    str += *globbed_files;
                    str += "\\vk_swiftshader_icd.json";

                    // I'm told this completely overrides the Vulkan driver selection logic.
                    // If we replace this with `VK_ADD_DRIVER_FILES`, then we could set this at startup before trying to initialize,
                    //   then initializing only once, and SDL should prefer hardware-accelerated implementations on its own.
                    // But that requires globbing always, and I'd rather not.
                    _putenv_s("VK_DRIVER_FILES", str.c_str());

                    // We don't check for errors here. We test if the handle is still null below.
                    state.device = SDL_CreateGPUDevice(SDL_ShaderCross_GetSPIRVShaderFormats(), state.debug_mode_enabled, nullptr);

                    state.must_manually_limit_fps = true;
                }
            }
            #else
            (void)params;
            #endif

            // If we couldn't fallback to an alternative implementation...
            if (!state.device)
                throw std::runtime_error(fmt::format("Unable to initialize the GPU device: {}", initial_error));
        }
    }

    Device::Device(Device &&other) noexcept
        : state(std::move(other.state))
    {
        other.state = {};
    }

    Device &Device::operator=(Device other) noexcept
    {
        std::swap(state, other.state);
        return *this;
    }

    Device::~Device()
    {
        if (state.device)
            SDL_DestroyGPUDevice(state.device);
    }
}
