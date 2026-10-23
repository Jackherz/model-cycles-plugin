#include "Randomise.h"
#include "PluginProcessor.h"
#include "Presets.h"

namespace cyclesemu
{
namespace
{
struct Draw
{
    juce::Random& r;
    bool chance(float p) { return r.nextFloat() < p; }
    int between(int lo, int hi) { return lo + r.nextInt(hi - lo + 1); }
    // a triangular draw around centre, +-spread
    int around(int centre, int spread)
    {
        return juce::jlimit(0, 127, centre + juce::roundToInt((r.nextFloat() - r.nextFloat()) * (float)spread));
    }
    template <typename T> T pick(std::initializer_list<T> xs) { return *(xs.begin() + r.nextInt((int)xs.size())); }
    // a macro knob: anywhere, or near one of its landmarks
    int macro() { return chance(0.5f) ? between(0, 127) : around(pick({0, 32, 64, 96, 127}), 10); }
    int bipolar(int lo, int hi) { return 64 + (chance(0.5f) ? 1 : -1) * between(lo, hi); }   // 64 +- lo..hi
};

void set(juce::AudioProcessorValueTreeState& st, int track, const char* id, int v)
{
    setPlain(st, paramId(track, id), (float)v);
}

int current(juce::AudioProcessorValueTreeState& st, int track, const char* id)
{
    auto* p = st.getParameter(paramId(track, id));
    return p ? juce::roundToInt(p->convertFrom0to1(p->getValue())) : 0;
}

// A variation: every 0..127 knob of the section moves a little (bipolar ones
// stay on their side unless near the centre); switches and choices stay.
void vary(juce::AudioProcessorValueTreeState& st, int track, std::initializer_list<const char*> ids, Draw& d)
{
    for (auto* id : ids)
    {
        auto* p = st.getParameter(paramId(track, id));
        if (p == nullptr || p->getNumSteps() < 100)
            continue;
        set(st, track, id, juce::jlimit(0, 127, current(st, track, id) + d.between(-10, 10)));
    }
}
} // namespace

void randomise(juce::AudioProcessorValueTreeState& st, int t, RandomSection s, juce::Random& rng, bool variation)
{
    Draw d{rng};
    switch (s)
    {
        case RandomSection::Sound:
            if (variation)
                return vary(st, t, {"decay", "color", "shape", "sweep", "contour", "voldist", "pan", "delaysend",
                                    "reverbsend"}, d);
            // pitch: mostly as programmed, else a musical interval
            set(st, t, "pitch", d.chance(0.65f) ? 64 : 64 + d.pick({-12, -7, -5, -3, 3, 5, 7, 12}));
            set(st, t, "decay", juce::jlimit(8, 124, d.around(d.pick({30, 60, 90}), 30)));
            for (auto* id : {"color", "shape", "sweep", "contour"})
                set(st, t, id, d.macro());
            set(st, t, "punch", d.chance(0.3f));
            set(st, t, "gate", d.chance(0.12f));
            set(st, t, "voldist", juce::jlimit(40, 110, d.around(64, 30)));
            set(st, t, "pan", d.chance(0.6f) ? 64 : d.bipolar(8, 40));
            set(st, t, "delaysend", d.chance(0.55f) ? 0 : d.between(12, 70));
            set(st, t, "reverbsend", d.chance(0.5f) ? 0 : d.between(12, 80));
            return;

        case RandomSection::Lfo:
        {
            if (variation)
                return vary(st, t, {"lfospeed", "lfodepth", "lfofade", "lfophase"}, d);
            set(st, t, "lfowave", d.between(0, 6));
            set(st, t, "lforeset", d.chance(0.5f) ? 1 : d.chance(0.6f) ? 0 : d.pick({2, 3, 4}));
            set(st, t, "lfosync", d.chance(0.8f) ? 0 : 1);
            set(st, t, "lfomult", d.pick({1, 2, 3, 4, 5, 6}));            // x2 .. x64
            const int speed = d.pick({8, 16, 16, 32, 32, 48, 63});
            set(st, t, "lfospeed", 64 + (d.chance(0.85f) ? speed : -speed));
            // where it goes, and how far: pitch gently, the rest fully
            static const int dests[] = {1, 2, 3, 4, 5, 9, 13, 14, 15};     // indices into kLfoDestNames
            const int dest = dests[rng.nextInt((int)std::size(dests))];
            set(st, t, "lfodest", dest);
            set(st, t, "lfodepth", dest == 1 ? d.bipolar(2, 8) : d.bipolar(10, 45));
            set(st, t, "lfofade", d.chance(0.7f) ? 64 : d.bipolar(10, 40));
            set(st, t, "lfophase", d.chance(0.6f) ? 0 : d.pick({32, 64, 96}));
            return;
        }

        case RandomSection::Filter:
        {
            // within the type chosen (the Cycles' own, or a Digitone II one)
            const int type = current(st, t, "ftype");
            if (variation)
                return type == 5 ? vary(st, t, {"fcut", "freso", "fenv", "fdec"}, d)   // Legacy: LP/HP stays
                                 : vary(st, t, {"fcut", "freso", "fextra", "fenv", "fdec"}, d);
            switch (type)
            {
                case 0:   // off, a moderate low-pass, or a gentle high-pass
                    set(st, t, "fcut", d.chance(0.3f) ? 64 : d.chance(0.65f) ? 64 - d.between(4, 30) : 64 + d.between(2, 20));
                    set(st, t, "freso", juce::jlimit(0, 100, d.around(30, 35)));
                    break;
                case 1:   // Multimode: mostly LP or BP, sometimes between or HP
                    set(st, t, "fcut", d.between(64, 112));
                    set(st, t, "freso", juce::jlimit(0, 90, d.around(40, 35)));
                    set(st, t, "fextra", juce::jlimit(0, 127, d.around(d.pick({0, 0, 32, 64, 64, 96, 127}), 6)));
                    break;
                case 2:   // Lowpass 4
                    set(st, t, "fcut", d.between(60, 112));
                    set(st, t, "freso", juce::jlimit(0, 100, d.around(45, 40)));
                    break;
                case 3:   // Equalizer: a clear boost or cut somewhere useful
                    set(st, t, "fcut", d.between(40, 110));
                    set(st, t, "freso", d.bipolar(16, 60));
                    set(st, t, "fextra", d.between(40, 100));
                    break;
                case 4:
                case 6:   // the combs: on a note related to C (fifths, octaves)
                    set(st, t, "fcut", d.pick({48, 55, 60, 67, 72, 79, 84, 91, 96}));
                    set(st, t, "freso", d.between(40, 110));
                    set(st, t, "fextra", d.between(60, 127));
                    break;
                default:  // Legacy: mostly low-pass
                    set(st, t, "fcut", d.between(60, 112));
                    set(st, t, "freso", juce::jlimit(0, 100, d.around(40, 35)));
                    set(st, t, "fextra", d.chance(0.75f) ? 0 : 127);
                    break;
            }
            set(st, t, "fenv", d.chance(0.5f) ? 64 : d.bipolar(8, 40));
            set(st, t, "fdec", d.between(30, 95));
            return;
        }
    }
}
} // namespace cyclesemu
