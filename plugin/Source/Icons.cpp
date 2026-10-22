#include "Icons.h"
#include "LfoShape.h"
#include <array>

namespace cyclesemu
{
namespace
{
constexpr int N = 9;
using Bits = std::array<std::array<bool, N>, N>;   // [row][col]

Bits fromRows(const char* const rows[N])
{
    Bits b{};
    for (int r = 0; r < N; ++r)
        for (int c = 0; c < N; ++c)
            b[(size_t)r][(size_t)c] = rows[r][c] == '1';
    return b;
}

// Kick: rings (a membrane). Snare: scattered noise. Metal: nested squares.
// Perc: a struck diamond. Tone: one sine cycle. Chord: stacked voices.
const char* const kMachineRows[6][N] = {
    {"001111100", "010000010", "100111001", "101000101", "101010101", "101000101", "100111001", "010000010",
     "001111100"},
    {"100100010", "001001000", "010000101", "000100100", "101001001", "000010000", "100100101", "001000010",
     "010010100"},
    {"111111111", "100000001", "101111101", "101000101", "101010101", "101000101", "101111101", "100000001",
     "111111111"},
    {"000010000", "000101000", "001000100", "010010010", "100111001", "010010010", "001000100", "000101000",
     "000010000"},
    {"000000000", "001100000", "010010000", "100001000", "100001001", "000000101", "000000110", "000000000",
     "000000000"},
    {"111111100", "000000000", "111110000", "000000000", "111111111", "000000000", "111100000", "000000000",
     "111111000"},
};
// Sync: a crotchet (follows the tempo). Free: an infinity loop.
const char* const kClockRows[2][N] = {
    {"000011000", "000010100", "000010010", "000010000", "000010000", "011110000", "111110000", "111110000",
     "011100000"},
    {"000000000", "000000000", "011000110", "100101001", "100010001", "100101001", "011000110", "000000000",
     "000000000"},
};

int rowFor(float v, bool unipolar)
{
    float y = unipolar ? (1.0f - v) : (1.0f - v) * 0.5f;
    return juce::jlimit(0, N - 1, juce::roundToInt(y * (N - 1)));
}

// A curve through columns from..to, joined with vertical runs.
void plot(Bits& b, int from, int to, const std::function<int(int)>& rowAt)
{
    int prev = -1;
    for (int c = from; c <= to; ++c)
    {
        int r = rowAt(c);
        if (prev >= 0)
            for (int y = juce::jmin(prev, r); y <= juce::jmax(prev, r); ++y)
                b[(size_t)y][(size_t)c] = true;
        b[(size_t)r][(size_t)c] = true;
        prev = r;
    }
}

Bits waveBits(int wave)
{
    Bits b{};
    bool uni = lfoUnipolar(wave);
    plot(b, 0, N - 1, [&](int c) { return rowFor(lfoWave(wave, (double)c / N), uni); });
    return b;
}

// Trig modes: a bar marks the note. Free: the wave ignores it. Trig: the
// wave restarts. Hold: the value freezes. One: one cycle, then still.
// Half: half a cycle, then still.
Bits trigBits(int mode)
{
    Bits b{};
    auto sine = [](double ph) { return rowFor(lfoWave(1, ph), false); };
    if (mode == 0)
    {
        plot(b, 0, N - 1, [&](int c) { return sine(c / 4.5 + 0.3); });
        return b;
    }
    for (int r = 0; r < N; ++r)
        b[(size_t)r][0] = true;
    switch (mode)
    {
        case 1: plot(b, 1, N - 1, [&](int c) { return sine((c - 1) / 8.0); }); break;
        case 2: plot(b, 1, N - 1, [&](int) { return 2; }); break;
        case 3: plot(b, 1, N - 1, [&](int c) { return c <= 6 ? sine((c - 1) / 5.0) : 4; }); break;
        default: plot(b, 1, N - 1, [&](int c) { return c <= 5 ? sine((c - 1) / 8.0) : 4; }); break;
    }
    return b;
}
} // namespace

void drawIcon(juce::Graphics& g, IconSet set, int index, juce::Rectangle<int> area, juce::Colour c, int scale)
{
    Bits b{};
    switch (set)
    {
        case IconSet::Machine: b = fromRows(kMachineRows[juce::jlimit(0, 5, index)]); break;
        case IconSet::Wave: b = waveBits(index); break;
        case IconSet::Trig: b = trigBits(index); break;
        case IconSet::Clock: b = fromRows(kClockRows[juce::jlimit(0, 1, index)]); break;
    }
    int x0 = area.getCentreX() - N * scale / 2, y0 = area.getCentreY() - N * scale / 2;
    g.setColour(c);
    for (int r = 0; r < N; ++r)
        for (int col = 0; col < N; ++col)
            if (b[(size_t)r][(size_t)col])
                g.fillRect(x0 + col * scale, y0 + r * scale, scale, scale);
}
} // namespace cyclesemu
