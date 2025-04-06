#include "sound.h"

#include "em/macros/utils/finally.h"

#include <SDL3/SDL_audio.h>
#include <vorbis/vorbisfile.h>

#include <fmt/format.h>

namespace em::Audio
{
    Sound::Sound(Format format, std::optional<Channels> expected_channel_count, em::Filesystem::LoadedFile input, BitResolution preferred_resolution)
    {
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
            }
            break;
          case ogg:
            try
            {
                SDL_IOStream *io = SDL_IOFromConstMem(input.data(), input.size());
                if (!io)
                    throw std::runtime_error("Failed to create SDL IO stream for the Vorbis file.");
                EM_FINALLY{ SDL_CloseIO(io); };

                // Construct callbacks.
                ov_callbacks callbacks;
                callbacks.close_func = nullptr;
                callbacks.tell_func = [](void *stream_ptr) -> long
                {
                    return SDL_TellIO((SDL_IOStream *)stream_ptr);
                };
                callbacks.seek_func = [](void *stream_ptr, std::int64_t offset, int mode) -> int
                {
                    // It's not specified anywhere, but `SDL_IOWhence` values seem to match the std modes exactly.
                    auto ret = SDL_SeekIO((SDL_IOStream *)stream_ptr, offset, (SDL_IOWhence)mode);
                    return ret == -1 ? -1 : 0;
                };
                callbacks.read_func = [](void *buffer, std::size_t elem_size, std::size_t elem_count, void *stream_ptr) -> std::size_t
                {
                    auto ret = SDL_ReadIO((SDL_IOStream *)stream_ptr, buffer, elem_count * elem_size);
                    return ret / elem_size;
                };

                // Open a file with those callbacks.
                OggVorbis_File ogg_file_handle;
                switch (ov_open_callbacks(&input, &ogg_file_handle, nullptr, 0, callbacks))
                {
                  case 0:
                    break;
                  case OV_EREAD:
                    throw std::runtime_error("Unable to read data from the stream.");
                    break;
                  case OV_ENOTVORBIS:
                    throw std::runtime_error("This is not a vorbis sound.");
                    break;
                  case OV_EVERSION:
                    throw std::runtime_error("Vorbis version mismatch.");
                    break;
                  case OV_EBADHEADER:
                    throw std::runtime_error("Invalid header.");
                    break;
                  case OV_EFAULT:
                    throw std::runtime_error("Internal vorbis error.");
                    break;
                  default:
                    throw std::runtime_error("Unknown vorbis error.");
                    break;
                }
                EM_FINALLY{ ov_clear(&ogg_file_handle); };


                // Get some info about the file. No cleanup appears to be necessary.
                vorbis_info *info = ov_info(&ogg_file_handle, -1);
                if (!info)
                    throw std::runtime_error("Unable to get information about the file.");

                // Get channel count.
                if (info->channels != 1 && info->channels != 2)
                    throw std::runtime_error("The file has too many channels. Only mono and stereo are supported.");
                channel_count = Channels(info->channels);
                CheckChannelCount();

                // Get frequency.
                sampling_rate = int(info->rate);

                // Get the block count.
                auto total_block_count = ov_pcm_total(&ogg_file_handle, -1);
                if (total_block_count == OV_EINVAL)
                    throw std::runtime_error("Unable to determine the file length.");

                // Copy bit resolution from the parameter.
                resolution = preferred_resolution;

                // Compute the necessary storage size.
                std::size_t storage_size = std::size_t(total_block_count * BytesPerBlock());

                data.resize(storage_size);

                std::size_t current_offset = 0;

                // An index of the current bitstream (basically a section) of the file.
                // When it changes, the amount of channels and/or sample rate can also change; if it happens, we throw an exception.
                int old_bitstream_index = -1;

                while (current_offset < storage_size)
                {
                    int bitstream_index;
                    auto segment_size = ov_read(&ogg_file_handle, reinterpret_cast<char *>(data.data() + current_offset), int(storage_size - current_offset), 0/*little endian*/,
                        BytesPerSample(), resolution == bits_16/*true means numbers are signed*/, &bitstream_index);

                    switch (segment_size)
                    {
                      case 0:
                        throw std::runtime_error("Unexpected end of file.");
                        break;
                      case OV_HOLE:
                        throw std::runtime_error("The file is corrupted.");
                        break;
                      case OV_EBADLINK:
                        throw std::runtime_error("Bad link.");
                        break;
                      case OV_EINVAL:
                        throw std::runtime_error("Invalid header.");
                        break;
                    }
                    current_offset += std::size_t(segment_size);

                    if (bitstream_index != old_bitstream_index)
                    {
                        old_bitstream_index = bitstream_index;

                        info = ov_info(&ogg_file_handle, -1); // `-1` means the current bitstream, we could also use `bitstream_index` here.
                        if (!info)
                            throw std::runtime_error("Unable to get information about a section of the file.");
                        if (info->channels != int(channel_count))
                            throw std::runtime_error("Channel count has changed in the middle of the file.");
                        if (info->rate != sampling_rate)
                            throw std::runtime_error("Sampling rate has changed in the middle of the file.");
                    }
                }

            }
            catch (std::exception &e)
            {
                throw std::runtime_error(fmt::format("While reading a vorbis sound from `{}`:\n{}", input.GetName(), e.what()));
            }
            break;
        }
    }
}
