// The LFO's waveforms, as the firmware's LFO runs them (measured on its
// output, NOTES.md step 45): what the editor draws and what LFO 2 plays.
// Triangle and sine start at 0 going up, square high first, saw falling from
// +1, Env a single fall from 1 after each note, Ramp a rise over the first
// half of each cycle, Random 16 held steps a cycle (its values are not the
// firmware's sequence). One and Half as the firmware runs them (step 47).
#pragma once

#include <cmath>
#include <cstdint>

namespace cyclesemu
{
// Env and Ramp run 0..1, the others -1..1.
inline bool lfoUnipolar(int wave) { return wave == 4 || wave == 5; }

// Env is not periodic: it falls once from a note, whatever the trig mode
inline bool lfoFromNote(int wave) { return wave == 4; }

// phase in cycles (any real number; Env: cycles since the note, from 0;
// Random uses whole steps as its seed)
inline float lfoWave(int wave, double phase)
{
    double p = phase - std::floor(phase);
    switch (wave)
    {
        case 0: return (float)(p < 0.25 ? 4.0 * p : p < 0.75 ? 2.0 - 4.0 * p : 4.0 * p - 4.0);
        case 1: return (float)std::sin(2.0 * 3.14159265358979 * p);
        case 2: return p < 0.5 ? 1.0f : -1.0f;
        case 3: return (float)(1.0 - 2.0 * p);
        case 4: return (float)std::exp(-8.34 * (phase > 0.0 ? phase : 0.0));
        case 5: return (float)(p < 0.5 ? 2.0 * p : 0.0);
        default:
        {
            // sample and hold, 16 steps a cycle
            auto step = (uint32_t)(int64_t)std::floor(phase * 16.0);
            uint32_t h = step * 2654435761u;
            h ^= h >> 15;
            h *= 2246822519u;
            h ^= h >> 13;
            return (float)(h & 0xffff) / 32767.5f - 1.0f;
        }
    }
}
// One (trig mode 3) and Half (4): from the note, run on to the waveform's
// end (One) or middle (Half) in the direction the LFO runs, then stop. A
// start phase at that point runs a whole cycle. Cycles from the note to the
// stop (start: the start phase in cycles).
inline double lfoOneShotLength(int mode, double start, bool backwards)
{
    const double to = mode == 3 ? 0.0 : 0.5;
    double d = backwards ? start - to : to - start;
    d -= std::floor(d);
    return d > 1e-9 ? d : 1.0;
}

// what One and Half hold once stopped (and before the first note): 0, but
// Ramp holds its peak after Half
inline float lfoStoppedValue(int wave, int mode) { return mode == 4 && wave == 5 ? 1.0f : 0.0f; }
} // namespace cyclesemu
