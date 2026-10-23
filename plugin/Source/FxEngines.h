// The delay's and reverb's engines (NOTES.md step 37): the firmware's own
// (Cycles), or one of the Schwung audio effects vendored in ThirdParty/,
// run in their place from the engine's render (mc_engine_set_fx).
#pragma once

#include <atomic>
#include <juce_core/juce_core.h>

namespace cyclesemu
{
extern const juce::StringArray kDelayTypeNames;    // Cycles, CHOMPI, Tape
extern const juce::StringArray kReverbTypeNames;   // Cycles, CloudSeed, CHOMPI, PSX
constexpr int kDelayTypes = 3, kReverbTypes = 4;

// The FX knobs as the engines read them (plain 0-127), and the tempo
struct FxKnobs
{
    int time = 32, feedback = 40, size = 64, tone = 64, delayx = 64, reverbx = 64;
    double bpm = 120.0;
};

class FxEngines
{
public:
    FxEngines() = default;
    ~FxEngines();
    // message thread: the engine for a type, created if it is not yet
    // (CloudSeed's lines are 37 MB, CHOMPI's buffers 7.7 MB)
    void ensure(int which, int type);
    bool ready(int which, int type) const;
    // audio thread, one 32-frame block (interleaved stereo): the type's
    // output, crossfaded (about 43 ms) from the type before after a change;
    // type 0 is the firmware's own output (fw)
    void render(int which, int type, const FxKnobs&, const float* in, const float* fw, float* out);
    bool fading(int which) const { return fade[which] < 1.0f; }

private:
    std::atomic<void*> engines[2][4] = {};
    int shown[2] = {0, 0}, from[2] = {0, 0};
    float fade[2] = {1.0f, 1.0f};
    FxKnobs applied[2][4];          // what each engine was last set to
    bool appliedOnce[2][4] = {};
    void run(int which, int type, const FxKnobs&, const float* in, const float* fw, float* out);
};
} // namespace cyclesemu
