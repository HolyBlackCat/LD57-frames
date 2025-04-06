#pragma once

#include <cassert>
#include <cmath>
#include <limits>
#include <memory>
#include <unordered_set>

#include "audio/buffer.h"
#include "audio/openal.h"
#include "em/math/vector.h"

namespace em::Audio
{
    enum class SourceState
    {
        initial, // Sources start in this state. Mostly equivalent to `stopped`.
        playing,
        paused,
        stopped,
    };

    class Source
    {
        inline static float
            default_rolloff_fac = 1,
            default_ref_dist    = 1,
            default_max_dist    = std::numeric_limits<float>::infinity();

        struct Data
        {
            ALuint handle = 0;
        };
        Data data;

        using ref = Source &;

      public:
        // Create a null source.
        Source() {}

        Source(const Audio::Buffer &buffer)
        {
            assert(buffer && "Attempt to use a null audio buffer.");

            // We don't throw if the handle is null. Instead, we make sure that any operation on a null handle has no effect.
            alGenSources(1, &data.handle);

            if (data.handle)
            {
                alSourcei(data.handle, AL_BUFFER, ALint(buffer.Handle()));

                alSourcef(data.handle, AL_REFERENCE_DISTANCE, default_ref_dist);
                alSourcef(data.handle, AL_ROLLOFF_FACTOR,     default_rolloff_fac);
                alSourcef(data.handle, AL_MAX_DISTANCE,       default_max_dist);
            }
        }

        Source(Source &&other) noexcept : data(std::exchange(other.data, {})) {}
        Source &operator=(Source other) noexcept
        {
            std::swap(data, other.data);
            return *this;
        }

        ~Source()
        {
            if (data.handle)
                alDeleteSources(1, &data.handle);
        }

        // Returns true if the source is not null.
        // Note that if a non-null source can't be constructed, you'll silently get a null source instead.
        // All operations on a null source should be no-ops.
        [[nodiscard]] explicit operator bool() const
        {
            return bool(data.handle);
        }

        // Returns the source handle.
        [[nodiscard]] ALuint Handle() const
        {
            return data.handle;
        }

        // Returns the source state, or `stopped` if null.
        [[nodiscard]] SourceState GetState() const
        {
            if (!data.handle)
                return SourceState::stopped;
            int state = 0;
            alGetSourcei(data.handle, AL_SOURCE_STATE, &state);
            switch (state)
            {
                case AL_INITIAL: return SourceState::initial;
                case AL_PLAYING: return SourceState::playing;
                case AL_PAUSED:  return SourceState::paused;
                case AL_STOPPED: return SourceState::stopped;
            }
            assert(false && "Unknown audio source state.");
            return SourceState::stopped;
        }

        [[nodiscard]] bool IsPlaying() const
        {
            return GetState() == SourceState::playing;
        }

        [[nodiscard]] bool IsLooping() const
        {
            if (!data.handle)
                return false;
            int ret = 0;
            alGetSourcei(data.handle, AL_LOOPING, &ret);
            return bool(ret);
        }


        // Sound model parameters.
        // See comments for `Audio::Parameters::Model` in `parameters.h` for the meaning of those settings.

        // Defaults to 1. Increase to make the sound lose volume with distance faster.
        static void DefaultRolloffFactor(float f)
        {
            default_rolloff_fac = f;
        }
        // Defaults to 1.
        static void DefaultRefDistance(float d)
        {
            default_ref_dist = d;
        }
        // Defaults to FLT_MAX (max finite float).
        static void DefaultMaxDistance(float d)
        {
            default_max_dist = d;
        }

        Source &rolloff_factor(float f)
        {
            if (data.handle)
                alSourcef(data.handle, AL_ROLLOFF_FACTOR, f);
            return *this;
        }
        Source &max_distance(float d)
        {
            if (data.handle)
                alSourcef(data.handle, AL_MAX_DISTANCE, d);
            return *this;
        }
        Source &ref_distance(float d)
        {
            if (data.handle)
                alSourcef(data.handle, AL_REFERENCE_DISTANCE, d);
            return *this;
        }


        // Common parameters.

        Source &volume(float v) // Defaults to 1.
        {
            if (data.handle)
                alSourcef(data.handle, AL_GAIN, v);
            return *this;
        }
        Source &pitch(float p) // Defaults to 0. The preferred range is -1..1.
        {
            if (data.handle)
                raw_pitch(std::exp2(p));
            return *this;
        }
        Source &raw_pitch(float p) // Defaults to 1, must be positive. The playback speed is multiplied by this number.
        {
            if (data.handle)
                alSourcef(data.handle, AL_PITCH, p);
            return *this;
        }
        Source &loop(bool l = true)
        {
            if (data.handle)
                alSourcei(data.handle, AL_LOOPING, l);
            return *this;
        }


        // State control.

        // Start playing.
        // If the source was paused, resumes from that position.
        // In any other case starts from the beginning (if the source was stopped or finished playing).
        Source &play()
        {
            if (data.handle)
                alSourcePlay(data.handle);
            return *this;
        }
        // Pause if playing.
        // If the source is playing, pauses it at the current position. Otherwise does nothing.
        Source &pause()
        {
            if (data.handle)
                alSourcePause(data.handle);
            return *this;
        }
        // Stop playing, and forget the current position.
        Source &stop()
        {
            if (data.handle)
                alSourceStop(data.handle);
            return *this;
        }
        // Same as `stop()`, but the state becomes `initial` rather than `stopped`.
        Source &rewind()
        {
            if (data.handle)
                alSourceRewind(data.handle);
            return *this;
        }


        // 3D audio support (makes sense for mono sources only).

        Source &pos(fvec3 p)
        {
            if (data.handle)
                alSourcefv(data.handle, AL_POSITION, &p.x);
            return *this;
        }
        Source &vel(fvec3 v)
        {
            if (data.handle)
                alSourcefv(data.handle, AL_VELOCITY, &v.x);
            return *this;
        }
        Source &relative(bool r = true)
        {
            if (data.handle)
                alSourcei(data.handle, AL_SOURCE_RELATIVE, r);
            return *this;
        }

        Source &pos(fvec2 p) {return pos(p.to_vec3());}
        Source &vel(fvec2 p) {return vel(p.to_vec3());}
    };
}
