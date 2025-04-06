#pragma once

#include <cassert>
#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>

#include "audio/buffer.h"
#include "audio/sound.h"
#include "em/meta/packs.h"
#include "em/meta/const_string.h"

// Provides singletones to conveniently load sounds.

namespace em::Audio::GlobalData
{
    template <typename T> concept ChannelsOrNullptr = em::Meta::same_as_any<T, Channels, std::nullptr_t>;
    template <typename T> concept FormatOrNullptr = em::Meta::same_as_any<T, Format, std::nullptr_t>;

    namespace impl
    {
        struct AutoLoadedBuffer
        {
            Buffer buffer;
            // Those may override the parameters specified when calling `LoadFiles()`.
            std::optional<Channels> channels_override;
            std::optional<Format> format_override;
        };

        // We rely on `std::map` never invalidating the references.
        using AutoLoadedBuffersMap = std::map<std::string, AutoLoadedBuffer, std::less<>>;

        [[nodiscard]] inline AutoLoadedBuffersMap &GetAutoLoadedBuffers()
        {
            static AutoLoadedBuffersMap ret;
            return ret;
        }

        template <Meta::ConstString Name, ChannelsOrNullptr auto ChannelCount, FormatOrNullptr auto FileFormat>
        struct RegisterBuffer
        {
            inline static const Buffer &ref = []() -> Buffer &
            {
                auto [iter, ok] = GetAutoLoadedBuffers().try_emplace(Name.str);
                assert(ok && "Attempt to register a duplicate auto-loaded sound file. This shouldn't be possible.");
                if constexpr (!std::is_null_pointer_v<decltype(ChannelCount)>)
                    iter->second.channels_override = ChannelCount;
                if constexpr (!std::is_null_pointer_v<decltype(FileFormat)>)
                    iter->second.format_override = FileFormat;
                return iter->second.buffer; // We rely on `std::map` never invalidating the references.
            }();
        };
    }

    // Returns a reference to a buffer, loaded from the filename passed as the parameter.
    // The load doesn't happen at the call point, and is done by `LoadFiles()`, which magically knows all files that it needs to load in this manner.
    // The returned reference is stable across reloads.
    template <Meta::ConstString Name, ChannelsOrNullptr auto ChannelCount = nullptr, FormatOrNullptr auto FileFormat = nullptr>
    [[nodiscard]] const Buffer &Sound()
    {
        return impl::RegisterBuffer<Name, ChannelCount, FileFormat>::ref;
    }

    // Same as `Sound()`, but without the optional parameters.
    template <Meta::ConstString Name>
    [[nodiscard]] const Buffer &operator""_sound()
    {
        return Sound<Name>();
    }

    // Loads (or reloads) all files requested with `Audio::GlobalData::Sound()`. Consider using the simplified overload, defined below.
    // The number of channels and the file format can be overridden by the `Sound()` calls.
    // `get_stream` is called repeatedly for all needed files.
    inline void Load(std::optional<Channels> channels, Format format, std::function<em::Filesystem::LoadedFile(const std::string &name, std::optional<Channels> channels, Format format)> get_stream)
    {
        for (auto &[name, data] : impl::GetAutoLoadedBuffers())
        {
            std::optional<Channels> file_channels = data.channels_override ? data.channels_override : channels;
            Format file_format = data.format_override.value_or(format);
            data.buffer = Audio::Sound(file_format, file_channels, get_stream(name, file_channels, file_format));
        }
    }

    // Same, but the sounds are loaded from files named `prefix + name + ext`,
    // where `name` comes from the `Sound()` call, and `ext` is determined from the format (`.wav` or `.ogg`).
    inline void Load(std::optional<Channels> channels, Format format, const std::string &prefix)
    {
        Load(channels, format, [&prefix](const std::string &name, std::optional<Channels> channels, Format format) -> em::Filesystem::LoadedFile
        {
            (void)channels;
            const char *ext = "";
            switch (format)
            {
                case wav: ext = ".wav"; break;
                case ogg: ext = ".ogg"; break;
            }
            return em::Filesystem::LoadedFile(prefix + name + ext);
        });
    }
}

namespace em::inline Common
{
    using Audio::GlobalData::operator""_sound;
}
