#include "sound.h"

#include "em/macros/utils/finally.h"

#include <SDL3/SDL_audio.h>

#include <fmt/format.h>

namespace em::Audio
{
    Sound::Sound(Format format, std::optional<Channels> expected_channel_count, em::Filesystem::LoadedFile input, BitResolution preferred_resolution)
    {
        (void)preferred_resolution;

        auto CheckChannelCount = [&]
        {
            if (expected_channel_count && *expected_channel_count != channel_count)
            {
                throw std::runtime_error(fmt::format("Expected a {} sound, but got {}.",
                    (*expected_channel_count == mono ? "mono" : "stereo"), (channel_count == mono ? "mono" : "stereo")));
            }
        };

        switch (format)
        {
          case wav:
            {
                SDL_AudioSpec spec{};
                std::uint8_t *bytes = nullptr;
                std::uint32_t len = 0;
                if (!SDL_LoadWAV_IO(SDL_IOFromConstMem(input.data(), input.size()), true, &spec, &bytes, &len))
                    throw std::runtime_error(fmt::format("Failed to parse wav file: `{}`.", input.GetName()));
                sampling_rate = spec.freq;
                channel_count = Channels(spec.channels);
                if (spec.format == SDL_AUDIO_U8)
                    resolution = bits_8;
                else if (spec.format == SDL_AUDIO_S16)
                    resolution = bits_16;
                else
                    throw std::runtime_error(fmt::format("Failed to parse wav file: `{}`. Expected 8 or 16 bits per sample, but it has some other format.", input.GetName()));

                data = {bytes, bytes + len};

                CheckChannelCount();
            }
            break;
          case ogg:
            break;
        }
    }
}
