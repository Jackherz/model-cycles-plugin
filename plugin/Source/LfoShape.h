// The LFO's waveforms as the editor draws them (icons and the LFO display).
// Triangle, sine and square are certain; the direction of saw and the
// shapes of Env, Ramp and Random follow other Elektron machines
// (approximate).
#pragma once

#include <cmath>
#include <cstdint>

namespace cyclesemu
{
// Env and Ramp run 0..1, the others -1..1.
inline bool lfoUnipolar(int wave) { return wave == 4 || wave == 5; }

// phase in cycles (any real number; Random uses whole cycles as its seed)
inline float lfoWave(int wave, double phase)
{
    double p = phase - std::floor(phase);
    switch (wave)
    {
        case 0: return (float)(p < 0.25 ? 4.0 * p : p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0);
        case 1: return (float)std::sin(2.0 * 3.14159265358979 * p);
        case 2: return p < 0.5 ? 1.0f : -1.0f;
        case 3: return (float)(2.0 * p - 1.0);
        case 4: return (float)std::exp(-5.0 * p);
        case 5: return (float)(1.0 - p);
        default:
        {
            // sample and hold, 8 steps a cycle
            auto step = (uint32_t)(int64_t)std::floor(phase * 8.0);
            uint32_t h = step * 2654435761u;
            h ^= h >> 15;
            h *= 2246822519u;
            h ^= h >> 13;
            return (float)(h & 0xffff) / 32767.5f - 1.0f;
        }
    }
}
} // namespace cyclesemu
