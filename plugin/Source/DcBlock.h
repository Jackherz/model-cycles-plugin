// The hardware's line outputs are AC-coupled; the firmware's output carries
// a slow DC drift (about -0.0002 of full scale a minute when idle, NOTES.md
// step 7). A first-order high-pass at 5 Hz, run at the engine's 48 kHz,
// stands in for the coupling capacitors. Shared by the plugin and its test.
#pragma once
#include <cmath>

struct DcBlock
{
    float x1 = 0.0f, y1 = 0.0f;
    // pole for a 5 Hz corner at 48 kHz
    static constexpr float r = 0.99934574f;   // exp(-2 * pi * 5 / 48000)

    void reset() { x1 = y1 = 0.0f; }

    void process(float* s, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            float y = s[i] - x1 + r * y1;
            x1 = s[i];
            y1 = y;
            s[i] = y;
        }
    }
};
