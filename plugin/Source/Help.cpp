// The text follows the Model:Cycles manual where it says something
// (machines, Punch, Gate, FX). Its per-machine meaning of Color, Shape,
// Sweep and Contour is not in the manual: those lines are approximate.
// The Syntakt engines (machines 6-10) name their knobs after the Syntakt's
// own parameters, as Modded-Cycles' guide maps them. SP TWINSHOT (11) has
// texts of its own, measured on its code (NOTES.md steps 38, 40).
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
                           "SD Vintage", "CP Vintage", "SY Toy", "SY Bits", "SY Swarm", "SP Twinshot"};

// SP TWINSHOT: what each knob is on it (measured on the Syntakt's own engine)
const std::map<juce::String, HelpText> kTwinshot = {
    {"pitch", {"Tune 1", "Sample 1's tuning, a semitone a step, two octaves either way. Notes you play transpose "
                         "both samples."}},
    {"decay", {"Decay 1", "How long Sample 1 plays: 0 is silent, 40 a short hit, 127 the whole sample. Sample 2 "
                          "always plays to its end."}},
    {"color", {"Sample 1", "The first sample, one of 64 from your Twinshot folder: Twinshot Samples in the Kit menu "
                           "(the Voice menu in CyclesVoice) chooses the folder, subfolders included, and with more than "
                           "64 files there, the first 64 by name or a random 64. From the middle up the samples play "
                           "forward, below the middle the same ones reversed (shown with <). A sound remembers a slot, "
                           "not a file: a new folder or pick changes what it plays."}},
    {"shape", {"Filter 1", "A filter on Sample 1 only. 64 leaves it open, lower is a low-pass that darkens it "
                           "down to a thud at 0, higher a high-pass that thins it out."}},
    {"sweep", {"Mix", "The balance of the two samples: 0 Sample 1 alone, 64 both equally, 127 Sample 2 alone."}},
    {"contour", {"Tune 2", "On page 2: Sample 2's tuning, a semitone a step from 40 to 88 (64 as recorded): "
                           "two octaves either way."}},
    {"punch", {"Punch", "Not used on SP Twinshot."}},
};

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
      "engines, running its own code, and SP Twinshot its two-sample player, with your own WAVs. The knobs change "
      "meaning with the machine."}},
    {"pitch",
     {"Pitch", "Tuning in semitones around the programmed note. Notes you play transpose from here."}},
    {"decay", {"Decay", "How long the sound rings after each note. Low is a short blip, high a long ring."}},
    {"punch", {"Punch", "Adds a fixed pseudo-compression and distortion, for a harder hit."}},
    {"gate",
     {"Gate", "Gate mode: the sound lasts as long as the note is held, instead of playing out its full decay."}},
    {"mute", {"Mute", "Silences this track, as a mixer mute: a ringing note goes quiet, and comes back on unmute."}},
    {"solo",
     {"Solo", "Plays only the soloed tracks: while any track is soloed, the others are silent. Solo several to "
              "hear them together."}},
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
    {"lfospeed",
     {"LFO Speed",
      "The LFO rate. Negative values run the waveform backwards. The knob snaps to the even rates (8, 16, 32, "
      "48, 63 either way) that divide into bars with the multiplier; hold Alt to turn freely."}},
    {"lfodepth",
     {"LFO Depth", "How far the LFO moves its destination. Negative values turn the waveform upside down."}},
    {"lfofade",
     {"LFO Fade",
      "Fades the LFO after each note: negative fades it in, positive fades it out. 0 is no fade."}},
    {"lfophase",
     {"LFO Start Phase",
      "Where in the waveform the LFO starts when a note restarts it. 0 is the start, 64 halfway."}},
    {"ftype",
     {"Filter Type",
      "Cycles: the original filter, low-pass left of centre, high-pass right, OFF in the middle. The others are the "
      "Digitone II's: Multimode (2-pole, MORPH from low-pass through band-pass to high-pass), Lowpass 4 (4-pole), "
      "Equalizer (a bell: GAIN boosts or cuts around FREQ, Q sets the width), Legacy (2-pole LP or HP), Comb- and "
      "Comb+ (a resonant comb tuned to a note; FDBK makes it ring, LPF darkens it). Picking a type here starts it "
      "open, flat or dry."}},
    {"fextra",
     {"Filter: Third Knob",
      "What it does depends on the type: Multimode's MORPH (LP, BP at the middle, HP), the Equalizer's Q (wide to "
      "narrow), the combs' LPF (dark to bright), Legacy's LP or HP. Unused on Cycles and Lowpass 4."}},
    {"fcut",
     {"Filter Cutoff / Freq",
      "Cycles type: left of centre a low-pass (L64 closed, 5 Hz, to L1 open), right a high-pass (H1 barely, to "
      "H63 at 19 kHz), OFF in the centre leaves the track untouched. Digitone II types: the frequency, 5 Hz to "
      "20 kHz (64 is about 330 Hz); for the combs, a note (72 is C4). After Vol+Dist. CC 74."}},
    {"freso",
     {"Filter Reso / Gain / Fdbk",
      "The resonance, from none to a sharp, singing peak. On the Equalizer, the gain (64 flat, about +-12 dB); on "
      "the combs, the feedback. CC 71."}},
    {"fenv",
     {"Filter Env",
      "How far an envelope moves the cutoff on each note, up or down (0 is none). It starts on every note "
      "and falls over Env Decay."}},
    {"fdec", {"Filter Env Decay", "How long the filter envelope takes to fall, from 5 ms to 10 seconds."}},
    {"port",
     {"Portamento",
      "Each note glides to its pitch from the last note played, over Time. Track menu > Glide Setup: the "
      "slope (a constant rate, Time per octave, or a constant time), Glissando (semitone steps), Legato Only "
      "(glide only from a key still held) and Gating (the glide stops when the key is let go)."}},
    {"legato",
     {"Legato",
      "A key pressed while another is held changes the pitch without starting the note again, and letting it "
      "go returns to the key still held. Which held key plays: Track menu > Glide Setup > Note Priority. "
      "CyclesVoice plays mono while it is on."}},
    {"ptim",
     {"Portamento Time",
      "How long the glide takes, from 1 ms to 10 s: the time for the whole glide, or per octave with the "
      "Constant Rate slope."}},
    {"pamount",
     {"Portamento Amount",
      "How much of the distance the glide covers: 100 glides all the way from the last note, 50 starts "
      "halfway there."}},
    {"soundsetup",
     {"Sound Setup: Velocity Mod and Key Tracking",
      "As the Syntakt's SOUND SETUP. VELOCITY MOD: up to four knobs that move with how hard a note is played, each by "
      "its depth (at full velocity the knob moves by the depth; negative depths move it the other way). KEY "
      "TRACKING: the same with the note played, centred on middle C (C3 in Live): depth 8 moves the knob a step "
      "per note, 127 sixteen. VEL > VOL: how velocity sets the volume (Lin as played, Log brings soft notes up, Exp favours hard "
      "ones, Off ignores it). FILT KEY: the filter follows the keyboard (127: fully)."}},
    {"velvol",
     {"Vel > Vol",
      "How the velocity of a note sets its volume. Lin: as played. Log: soft notes come up, so the dynamics are "
      "gentler. Exp: hard notes stand out more. Off: every note at full volume."}},
    {"fkey",
     {"Filter Keytrack",
      "The filter cutoff follows the note played, centred on middle C (C3 in Live): at 127 it moves a semitone per "
      "note, so the tone stays the same up the keyboard. 0: the cutoff stays put."}},
    {"twsamp2",
     {"Sample 2",
      "SP Twinshot plays two samples at once. This is the second one, on page 2 (the PAGE button switches), from the "
      "same 64 as Sample 1 (Twinshot Samples in the Kit or Voice menu): forward from the middle up, reversed below it "
      "(shown with <). Tune 2 beside it tunes it, Mix on page 1 balances the two."}},
    {"amp",
     {"Amp Envelope",
      "The Syntakt's amp envelope, running its own code (from your Syntakt OS), after the filter and before level, "
      "pan and the sends. Off by default: the track plays as it always did. The AMP button in the FILTER header "
      "shows its page. It shapes what the machine plays: it cannot bring back a sound the machine's own Decay has "
      "ended, so set the machine's Decay long for slow envelopes. Before its first note it lets the track through."}},
    {"amode",
     {"Amp Mode",
      "AHD: attack, hold, decay, like a drum (a fixed hold ignores the key, HOLD at NOTE follows it). ADSR: attack, "
      "decay to the sustain level while the key is held, then release (no hold stage). Picking a machine sets it as "
      "the Syntakt does: ADSR for the SY machines, AHD for the others."}},
    {"aatk",
     {"Amp Attack",
      "A straight rise to full level. 0 is instant, 32 about 48 ms, 64 about 0.26 s, 96 about 0.7 s, 127 30 s."}},
    {"ahold",
     {"Amp Hold",
      "AHD only: how long it stays at full level before the decay. 16 about 64 ms, 64 about 1.8 s, 126 about 10 s, "
      "the key ignored. At NOTE (127) it holds while the key is down."}},
    {"adec",
     {"Amp Decay",
      "An exponential fall: to the sustain level in ADSR, to silence in AHD. Time to -60 dB: 8 about 40 ms, 32 "
      "about 1 s, 64 about 5.5 s, 126 about 30 s. INF (127): it never falls."}},
    {"asus",
     {"Amp Sustain",
      "ADSR only: the level held while the key is down, in proportion to the value: 64 is half (-6 dB), 100 about "
      "-2 dB, 127 full."}},
    {"arel",
     {"Amp Release",
      "ADSR: the fall to silence after the key is let go, the same law as the decay: 8 about 40 ms, 32 about 1 s, "
      "64 about 5.5 s to -60 dB."}},
    {"aenr",
     {"Amp Env Reset",
      "On (the Syntakt's default): each new note fades the old level out over 0.7 ms and starts the attack from "
      "silence. Off: a new note carries on from the level it is at."}},
    {"l2dest",
     {"LFO 2 Destination",
      "LFO 2 is the plugin's own, beside the firmware's LFO (the LFO 2 button in the LFO header shows it). It adds "
      "to the knob, after velocity and key tracking, without moving the knob itself. Off by default. It moves "
      "the sound and filter knobs (Pitch in semitones), and works the same with LFO 1 on the same knob: the two add."}},
    {"l2wave", {"LFO 2 Waveform", "Triangle, sine, square, saw, Env (one way down), ramp, or random steps, as drawn."}},
    {"l2reset",
     {"LFO 2 Trig Mode",
      "What a note does, as on LFO 1 (measured there): Free, nothing. Trig, restarts at the start phase. Hold, "
      "freezes the value it had at the note. One, plays one cycle from the note and stops. Half, half a cycle."}},
    {"l2sync",
     {"LFO 2 Sync", "Synced: the rate follows the tempo. Free: the same rates as if at 120 BPM, whatever the tempo."}},
    {"l2mult", {"LFO 2 Multiplier", "Multiplies the rate, 1 to 2k: speed x multiplier / 128 cycles a bar, as LFO 1."}},
    {"l2speed",
     {"LFO 2 Speed", "The rate, with the multiplier: 32 x 4 is one cycle a bar. Negative runs the waveform backwards."}},
    {"l2depth",
     {"LFO 2 Depth",
      "How far it moves the knob: each step is 2 knob steps either way, as LFO 1 (32 sweeps the whole range). "
      "Negative turns the waveform upside down. No fade yet: LFO 1's fade law is not measured well enough to copy."}},
    {"l2phase", {"LFO 2 Start Phase", "Where in the waveform a note restarts it (Trig, One, Half). 64 is halfway."}},
    {"dice",
     {"Randomise",
      "Draws new settings for this section, weighted toward sweet spots: musical pitches and LFO rates, sends "
      "often off, the filter usually off or moderate. The machine, level and mute stay. Shift-click: a "
      "variation, nudging the current values instead. Track and Kit menus: save, load, copy and paste."}},
    {"delaytime",
     {"Delay Time",
      "The delay time, synced to tempo: 32 is one beat. Note lengths show by name (1/16, 1/8., 1/4...) and the "
      "knob snaps to them as you turn it; hold Alt to turn freely."}},
    {"delaytype",
     {"Delay Type",
      "Cycles: the Model:Cycles' own delay. CHOMPI: CHOMPI TEMPO's dual delay (open source, MIT): WAND down from the "
      "middle is a clean echo locked to the tempo, from 1 bar to 1/16; up from the middle it melts into reverb; "
      "RANDOM makes repeats jump an octave, reverse or retrigger. Tape: a ping-pong tape echo (Schwung's Space "
      "Delay, MIT) with TONE darkening the repeats."}},
    {"delayx", {"Delay: Third Knob", "CHOMPI: Random (octave, reverse and retrigger jumps). Tape: Tone."}},
    {"reverbtype",
     {"Reverb Type",
      "Cycles: the Model:Cycles' own reverb. CloudSeed: Ghost Note Audio's lush algorithmic reverb (MIT): DECAY, "
      "HI CUT and SPACE (the room's size). CHOMPI: CHOMPI TEMPO's reverb (Mutable Instruments' Clouds reverb, MIT): "
      "TIME, LO-PASS and DIFFUSE. PSX: the PlayStation's SPU reverb (MIT emulation): DECAY, the MODEL (Room, "
      "Studio S/M/L, Hall, Space Echo) and LEVEL."}},
    {"reverbx", {"Reverb: Third Knob", "CloudSeed: Space (room size). CHOMPI: Diffusion. PSX: Level."}},
    {"delayfeedback",
     {"Delay Feedback", "How much of the delay feeds back into itself. High values repeat for a long time."}},
    {"reverbsize",
     {"Reverb Size",
      "How long the reverb rings. 127 plays as 126 for now: emulated, it never fades, while the hardware's "
      "does."}},
    {"reverbtone", {"Reverb Tone", "The reverb's colour, from dark to bright."}},
    {"midimode",
     {"MIDI Mode",
      "Drum Map: on any channel, C1 to G1 play tracks 1 to 8, and notes from C2 up play the Key Trk track "
      "(note names as Live shows them: middle C, note 60, is C3). "
      "Ch 1-8: MIDI channels 1 to 8 play tracks 1 to 8, as the hardware does for its six."}},
    {"fxdelay", {"Delay Send", "How much of the input goes into the delay. Its return also feeds the reverb."}},
    {"fxreverb", {"Reverb Send", "How much of the input goes into the reverb."}},
    {"fxmix",
     {"Mix",
      "The balance of the dry input and the effects. 100 (the default) is effects only, for a send/return "
      "channel; lower it to use CyclesFX as an insert."}},
    {"voices",
     {"Voices",
      "CyclesVoice plays one sound on up to eight voices (two engines of six tracks, its own effects shared). "
      "This sets how many it uses: 1 is mono (each note cuts the last), 8 lets chords and tails overlap. The "
      "oldest voice is reused first."}},
    {"keystrack",
     {"Key Trk", "In Drum Map mode, the track that notes from C2 up play, chromatically (C1 to G1 play the eight "
                 "tracks)."}},
    {"followhost",
     {"Host Tempo",
      "On: the engine follows your DAW's tempo. Off: the Tempo knob sets it, even inside a DAW."}},
    {"tempo",
     {"Tempo",
      "The engine's tempo in BPM. Delay time and synced LFOs follow it. With HOST lit it follows your DAW. "
      "Click HOST to set it here: drag the display up or down, or click the arrows for 1 BPM steps (shift: "
      "finer). The bars show the output level."}},
    {"outgain", {"Output", "Output gain in dB, after the engine."}},
};
} // namespace

HelpText helpFor(const juce::String& id, int machine)
{
    // strip "t3_" or "fx_"
    auto bare = id;
    if (id.startsWith("fx_") || (id.length() > 3 && id[0] == 't' && id[2] == '_'))
        bare = id.substring(3);
    machine = juce::jlimit(0, 11, machine);
    if (machine == 11)
        if (auto t = kTwinshot.find(bare); t != kTwinshot.end())
            return t->second;
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
    // the SOUND SETUP slots: Velocity Mod (vm) and Key Tracking (km), 1-4
    if (bare.length() == 6 && (bare.startsWith("vm") || bare.startsWith("km")) && bare[2] >= '1' && bare[2] <= '4')
    {
        const bool vel = bare.startsWith("vm"), dest = bare.endsWith("dst");
        const juce::String slot = juce::String::charToString(bare[2]);
        if (dest)
            return {(vel ? "Velocity Mod " : "Key Tracking ") + slot + ": Destination",
                    vel ? "The knob this slot moves with how hard a note is played. Off: none. The slots add up when "
                          "two pick the same knob."
                        : "The knob this slot moves with the note played, up above middle C (C3 in Live) and down "
                          "below. Off: none. The slots add up when two pick the same knob."};
        return {(vel ? "Velocity Mod " : "Key Tracking ") + slot + ": Depth",
                vel ? "How far the destination moves at full velocity, added to its knob: softer notes move it "
                      "less. Negative depths move it down."
                    : "How far the destination moves per note from middle C: 8 is a step a note, 127 sixteen. "
                      "Negative depths move it the other way."};
    }
    if (auto h = kHelp.find(bare); h != kHelp.end())
        return h->second;
    return {};
}
} // namespace cyclesemu
