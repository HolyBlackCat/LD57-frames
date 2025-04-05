#pragma once

#include <SDL3/SDL_timer.h>

#include <cmath>
#include <cstdint>

namespace Clock
{
    [[nodiscard]] inline std::uint64_t Time()
    {
        return SDL_GetPerformanceCounter();
    }

    [[nodiscard]] inline std::uint64_t TicksPerSecond()
    {
        static std::uint64_t ret = SDL_GetPerformanceFrequency();
        return ret;
    }

    [[nodiscard]] inline std::uint64_t SecondsToTicks(double secs)
    {
        return std::uint64_t(secs * TicksPerSecond());
    }
    [[nodiscard]] inline double TicksToSeconds(std::uint64_t ticks)
    {
        return ticks / double(TicksPerSecond());
    }

    inline void WaitSeconds(double secs) // Waits the specified amount of seconds, rounded down to milliseconds.
    {
        SDL_Delay(std::uint32_t(secs * 1000));
    }

    class DeltaTimer
    {
        std::uint64_t time = 0;
      public:
        DeltaTimer() : time(Time()) {} // Don't call before SDL initialization.

        std::uint64_t operator()()
        {
            std::uint64_t new_time = Time(), delta = new_time - time;
            time = new_time;
            return delta;
        }

        std::uint64_t LastTimePoint() const
        {
            return time;
        }
    };
}
