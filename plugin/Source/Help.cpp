// The text follows the Model:Cycles manual where it says something
// (machines, Punch, Gate, FX). Its per-machine meaning of Color, Shape,
// Sweep and Contour is not in the manual: those lines are approximate.
// The Syntakt engines (machines 6-10) name their knobs after the Syntakt's
// own parameters, as Modded-Cycles' guide maps them.
// Write only characters the pixel font has (Retro.cpp): no quotes or
// semicolons.
#include "Help.h"
#include <array>
#include <map>

namespace cyclesemu
{
namespace
{
const char* kMachines[] = {"Kick", "Snare", "Metal", "Perc", "Tone", "Chord",
                           "SD Vintage", "CP Vintage", "SY Toy", "SY Bits", "SY Swarm"};

// The Syntakt engines: the parameter each knob is (color, shape, sweep,
// contour, punch), from Modded-Cycles' guide.
const char* kSyntaktKnobs[5][5] = {
    {"Inharm", "Freq Complex", "Pitch Sweep", "Mod Envelope", "Punch"},
    {"Body Char", "Balance", "Spacing Crunch", "Body Envelope", "Punch"},
    {"Form", "Impact", "Bright", "Partial Decay", "Punch"},
    {"Detune (40 to 88)", "Balance", "Rate Redux", "Waveform", "Bit Redux"},
    {"Noise Mod", "Detune Anim", "Detune", "Osc Mix", "Sub-octave"},
};

// Per machine (Kick, Snare, Metal, Perc, Tone, Chord), roughly.
const std::map<juce::String, std::array<const char*, 6>> kMacros = {
    {"color",
     {"the body's brightness, from a soft sine kick toward a hard, snappy one.",
      "the balance of tone and noise.",
      "detunes its oscillators, from clean to clangorous.",
      "the timbre, from soft to harsh.",
      "the FM timbre, brighter as it rises.",
      "the timbre of the four chord voices."}},
    {"shape",
     {"the character of the attack.",
      "the snap of the noise.",
      "the metallic timbre.",
      "the FM character of the hit.",
      "the waveform, through the FM ratio.",
      "which chord plays (the manual's chord appendix lists them)."}},
    {"sweep",
     {"how far the pitch drops at the start: the kick's thump.",
      "the pitch drop of the tone.",
      "the pitch sweep at the start.",
      "the pitch sweep at the start.",
      "the pitch sweep at the start.",
      "a sweep of the chord's brightness."}},
    {"contour",
     {"how fast the pitch drop happens.",
      "the length of the noise tail.",
      "how the ring decays.",
      "the shape of the envelope.",
      "how the timbre moves over the note.",
      "how the sweep moves over the note."}},
};

const std::map<juce::String, HelpText> kHelp = {
    {"machine",
     {"Machine",
      "The sound engine for this track. Kick, Snare, Metal, Perc, Tone and Chord are the Model:Cycles' own, six "
      "mappings of one FM engine. SD Vintage, CP Vintage, SY Toy, SY Bits and SY Swarm are the Syntakt's digital "
      "engines, running its own code. The knobs change meaning with the machine."}},
    {"pitch",
     {"Pitch", "Tuning in semitones around the programmed note. Notes you play transpose from here."}},
    {"decay", {"Decay", "How long the sound rings after each note. Low is a short blip, high a long ring."}},
    {"punch", {"Punch", "Adds a fixed pseudo-compression and distortion, for a harder hit."}},
    {"gate",
     {"Gate", "Gate mode: the sound lasts as long as the note is held, instead of playing out its full decay."}},
    {"mute", {"Mute", "Silences this track."}},
    {"voldist",
     {"Volume + Distortion", "Turns the track up, and drives it into distortion as you go higher."}},
    {"pan", {"Pan", "Position in the stereo field. C is centre, L64 hard left, R63 hard right."}},
    {"delaysend", {"Delay Send", "How much of this track goes to the shared delay (the FX page)."}},
    {"reverbsend", {"Reverb Send", "How much of this track goes to the shared reverb (the FX page)."}},
    {"level",
     {"Level", "The track's level in the mix. Balances tracks without changing their distortion."}},
    {"lfowave",
     {"LFO Waveform",
      "The LFO's shape: triangle, sine, square, saw, Env (a one-way envelope), ramp, or random steps."}},
    {"lforeset",
     {"LFO Trig Mode",
      "What a note does to the LFO. Free: nothing, it keeps running. Trig: restarts it. Hold: freezes it at "
      "its current value. One: plays one cycle, then stops. Half: plays half a cycle. (Names as on other "
      "Elektron machines, unverified here.)"}},
    {"lfosync",
     {"LFO Sync",
      "Sync: the LFO rate follows the tempo. Free: the same rates, fixed as if at 120 BPM, whatever the "
      "tempo."}},
    {"lfomult",
     {"LFO Multiplier",
      "Multiplies the LFO rate, from 1 to 2k. Speed and Multiplier together set how fast it runs."}},
    {"lfodest",
     {"LFO Destination", "The parameter the LFO moves: any of this track's sound settings, or Off."}},
    {"lfospeed", {"LFO Speed", "The LFO rate. Negative values run the waveform backwards."}},
    {"lfodepth",
     {"LFO Depth", "How far the LFO moves its destination. Negative values turn the waveform upside down."}},
    {"lfofade",
     {"LFO Fade",
      "Fades the LFO after each note: negative fades it in, positive fades it out. 0 is no fade."}},
    {"lfophase",
     {"LFO Start Phase",
      "Where in the waveform the LFO starts when a note restarts it. 0 is the start, 64 halfway."}},
    {"delaytime",
     {"Delay Time", "The delay time, synced to tempo: 32 is one beat, 64 two beats."}},
    {"delayfeedback",
     {"Delay Feedback", "How much of the delay feeds back into itself. High values repeat for a long time."}},
    {"reverbsize",
     {"Reverb Size",
      "How long the reverb rings. 127 plays as 126 for now: emulated, it never fades, while the hardware's "
      "does."}},
    {"reverbtone", {"Reverb Tone", "The reverb's colour, from dark to bright."}},
    {"midimode",
     {"MIDI Mode",
      "Drum Map: on any channel, C1 to F1 play tracks 1 to 6, and notes from C3 up play the Key Trk track. "
      "Ch 1-6: MIDI channels 1 to 6 play tracks 1 to 6, as on the hardware."}},
    {"keystrack",
     {"Key Trk", "In Drum Map mode, the track that notes from C3 up play, chromatically."}},
    {"followhost",
     {"Host Tempo",
      "On: the engine follows your DAW's tempo. Off: the Tempo knob sets it, even inside a DAW."}},
    {"tempo",
     {"Tempo", "The engine's tempo in BPM when Host Tempo is off. Delay time and synced LFOs follow it."}},
    {"outgain", {"Output", "Output gain in dB, after the engine."}},
};
} // namespace

HelpText helpFor(const juce::String& id, int machine)
{
    // strip "t3_" or "fx_"
    auto bare = id;
    if (id.startsWith("fx_") || (id.length() > 3 && id[0] == 't' && id[2] == '_'))
        bare = id.substring(3);
    machine = juce::jlimit(0, 10, machine);
    const char* knobs[] = {"color", "shape", "sweep", "contour", "punch"};
    for (int k = 0; machine >= 6 && k < 5; ++k)
        if (bare == knobs[k])
            return {kSyntaktKnobs[machine - 6][k],
                    "On the " + juce::String(kMachines[machine]) + " machine, the Syntakt engine of that name, "
                    "this knob is the Syntakt's own " + kSyntaktKnobs[machine - 6][k] + " parameter" +
                    (k == 4 ? juce::String(" (on or off).") : juce::String("."))};
    if (auto m = kMacros.find(bare); m != kMacros.end())
    {
        auto title = bare.substring(0, 1).toUpperCase() + bare.substring(1);
        return {title, "A macro: one knob that moves several FM settings at once. On the " +
                           juce::String(kMachines[machine]) + " machine it sets " + m->second[(size_t)machine] +
                           " (Approximate: the manual does not spell these out.)"};
    }
    if (auto h = kHelp.find(bare); h != kHelp.end())
        return h->second;
    return {};
}
} // namespace cyclesemu
