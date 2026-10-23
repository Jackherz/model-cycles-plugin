// CyclesEmu: the Model:Cycles engine (the firmware's own render, compiled
// ahead of time, ../aot) as an instrument.
//
// MIDI, in one of two modes (parameter "MIDI Mode"):
//  - Drum Map (default, for DAWs that send one channel): on any channel,
//    C1-F1 (notes 36-41) trigger tracks 1-6 at their own pitch (note 60, C4,
//    is the programmed pitch), and notes from C3 (48) up play the "Keys
//    Track" chromatically.
//  - Ch 1-6 = Tracks: channels 1-6 play tracks 1-6 chromatically, as the
//    hardware does.
// CCs on channels 1-6 set that track (NOTES.md, CC table), clamped as the
// firmware clamps them; FX CCs work on any of those channels.
//
// Tempo: the host's BPM when "Follow Host Tempo" is on and the host has
// one, else the Tempo parameter, on the engine's internal clock.
//
// Every sound and FX parameter is a host parameter, initialised from the
// loaded kit. LFOs as on the hardware: waveform, multiplier with a sync
// switch (the firmware's x1..x2k are tempo-synced, 1..2k run free at the
// 120 BPM rate: measured, NOTES.md step 11), destination, trig mode, speed,
// depth, fade, start phase.
//
// The engine renders 32-frame blocks at 48 kHz; events take effect at the
// next block boundary, as on the hardware's control rate. At other host
// rates the 48 kHz stream is resampled.

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"
#include <cstring>

namespace cyclesemu
{
// 6-10: the Syntakt's digital engines, added by Modded-Cycles' tweak
// syntakt-sd-cp-toy-bits-swarm (NOTES.md step 16)
const juce::StringArray kMachineNames{"Kick", "Snare", "Metal", "Perc", "Tone", "Chord",
                                      "SD Vintage", "CP Vintage", "SY Toy", "SY Bits", "SY Swarm"};
// As the firmware draws them: triangle, sine, square, saw, ENV, ramp, random.
const juce::StringArray kLfoWaveNames{"Triangle", "Sine", "Square", "Saw", "Env", "Ramp", "Random"};
// Elektron's usual order; the Model:Cycles draws these as icons (unverified).
const juce::StringArray kLfoTrigNames{"Free", "Trig", "Hold", "One", "Half"};
const juce::StringArray kLfoMultNames{"1", "2", "4", "8", "16", "32", "64", "128", "256", "512", "1k", "2k"};
// The firmware's destination values and the names its LFO page shows.
// 28-31: the per-track filter's parameters (Modded-Cycles' track-filter,
// NOTES.md step 17), which it adds to the LFO's list
const juce::StringArray kFilterTypeNames{"Cycles", "Multimode", "Lowpass 4", "Equalizer", "Legacy", "Comb-", "Comb+"};
const FilterTypeInfo kFilterTypes[7] = {
    {"Cutoff", "Reso", "", 64, 0, 0},
    {"Freq", "Reso", "Morph", 127, 0, 0},
    {"Freq", "Reso", "", 127, 0, 0},
    {"Freq", "Gain", "Q", 64, 64, 64},
    {"Freq", "Reso", "LP/HP", 127, 0, 0},
    {"Freq", "Fdbk", "LPF", 72, 0, 127},
    {"Freq", "Fdbk", "LPF", 72, 0, 127},
};
const int kLfoDestValues[kNumLfoDests] = {0, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 28, 29, 30, 31};
const juce::StringArray kLfoDestNames{"Off", "Pitch", "Color", "Shape", "Sweep", "Contour", "Punch",
                                      "Gate", "Fine Tune", "Decay", "Vol+Dist", "Delay Send",
                                      "Reverb Send", "Pan", "Filt Cutoff", "Filt Reso", "Filt Env",
                                      "Filt Decay"};

// Record offsets, CCs and clamps: NOTES.md and scripts/engine_inputs.py.
const ParamDef kTrackParams[] = {
    {"machine", "Machine", 0x26, 0, 10, 70, Kind::Sound, Show::Plain},
    {"pitch", "Pitch", 0x28, 40, 88, 65, Kind::Sound, Show::Semitones},
    {"decay", "Decay", 0x38, 0, 127, 80, Kind::Sound, Show::Plain},
    {"color", "Color", 0x2a, 0, 127, 16, Kind::Sound, Show::Plain},
    {"shape", "Shape", 0x2c, 0, 127, 17, Kind::Sound, Show::Plain},
    {"sweep", "Sweep", 0x2e, 0, 127, 18, Kind::Sound, Show::Plain},
    {"contour", "Contour", 0x30, 0, 127, 19, Kind::Sound, Show::Plain},
    {"punch", "Punch", 0x32, 0, 1, 66, Kind::Sound, Show::Plain},
    {"gate", "Gate", 0x34, 0, 1, 67, Kind::Sound, Show::Plain},
    {"voldist", "Volume+Dist", 0x3a, 0, 127, 7, Kind::Sound, Show::Plain},
    {"pan", "Pan", 0x40, 0, 127, 10, Kind::Sound, Show::Pan},
    {"delaysend", "Delay Send", 0x3c, 0, 127, 12, Kind::Sound, Show::Plain},
    {"reverbsend", "Reverb Send", 0x3e, 0, 127, 13, Kind::Sound, Show::Plain},
    {"level", "Level", 0, 0, 127, 95, Kind::Level, Show::Plain},
    {"mute", "Mute", 0, 0, 1, 94, Kind::Mute, Show::Plain},
    {"solo", "Solo", 0, 0, 1, -1, Kind::Solo, Show::Plain},
    {"lfowave", "LFO Waveform", 0x1e, 0, 6, 106, Kind::Sound, Show::Plain},
    {"lfosync", "LFO Sync", 0x18, 0, 1, -1, Kind::LfoSync, Show::Plain},
    {"lfomult", "LFO Multiplier", 0x18, 0, 11, 103, Kind::LfoMult, Show::Plain},
    {"lfodest", "LFO Destination", 0x1c, 0, kNumLfoDests - 1, 105, Kind::LfoDest, Show::Plain},   // index into kLfoDestValues
    {"lforeset", "LFO Trig Mode", 0x22, 0, 4, 108, Kind::Sound, Show::Plain},
    {"lfospeed", "LFO Speed", 0x16, 0, 127, 102, Kind::Sound, Show::Bipolar},
    {"lfodepth", "LFO Depth", 0x24, 0, 127, 109, Kind::Sound, Show::Bipolar},
    {"lfofade", "LFO Fade", 0x1a, 0, 127, 104, Kind::Sound, Show::Bipolar},
    {"lfophase", "LFO Start Phase", 0x20, 0, 127, 107, Kind::Sound, Show::Plain},
    // the filter's type: the Cycles' own (Modded-Cycles), or one of the
    // Digitone II's (NOTES.md step 31), and its third knob; before the
    // knobs, so a preset sets the type first
    {"ftype", "Filter Type", 0, 0, 6, -1, Kind::Filter, Show::Plain},
    {"fextra", "Filter Morph/Q/LPF", 0, 0, 127, -1, Kind::Filter, Show::Plain},
    // the per-track filter (k 28-31): cutoff L64..OFF..H63, reso, envelope
    // amount (64 = none) and decay; CC 74 and 71 as on the Model:Samples
    {"fcut", "Filter Cutoff", 0x4c, 0, 127, 74, Kind::Sound, Show::Cutoff},
    {"freso", "Filter Reso", 0x4e, 0, 127, 71, Kind::Sound, Show::Plain},
    {"fenv", "Filter Env", 0x50, 0, 127, -1, Kind::Sound, Show::Bipolar},
    {"fdec", "Filter Env Decay", 0x52, 0, 127, -1, Kind::Sound, Show::Plain},
    // legato and portamento (the plugin's): PORT, PTIM and AMOUNT, LEGATO,
    // and the Syntakt's portamento setup (NOTES.md step 28). The order here
    // is glideIds' below.
    {"port", "Portamento", 0, 0, 1, -1, Kind::Glide, Show::Plain},
    {"ptim", "Portamento Time", 0, 0, 127, -1, Kind::Glide, Show::GlideTime},
    {"pamount", "Portamento Amount", 0, 0, 100, -1, Kind::Glide, Show::Plain},
    {"legato", "Legato", 0, 0, 1, -1, Kind::Glide, Show::Plain},
    {"notepri", "Note Priority", 0, 0, 2, -1, Kind::Glide, Show::Plain},
    {"pslope", "Portamento Slope", 0, 0, 1, -1, Kind::Glide, Show::Plain},
    {"pstyle", "Portamento Style", 0, 0, 1, -1, Kind::Glide, Show::Plain},
    {"plegonly", "Portamento Legato Only", 0, 0, 1, -1, Kind::Glide, Show::Plain},
    {"pgating", "Portamento Gating", 0, 0, 1, -1, Kind::Glide, Show::Plain},
};
const int kNumTrackParams = (int)(sizeof kTrackParams / sizeof kTrackParams[0]);

const ParamDef kFxParams[] = {
    {"delaytime", "Delay Time", 0x12, 0, 127, 85, Kind::Fx, Show::NoteLength},
    {"delayfeedback", "Delay Feedback", 0x14, 0, 127, 86, Kind::Fx, Show::Plain},
    {"reverbsize", "Reverb Size", 0x16, 0, 127, 87, Kind::Fx, Show::Plain},
    {"reverbtone", "Reverb Tone", 0x18, 0, 127, 88, Kind::Fx, Show::Plain},
};
const int kNumFxParams = (int)(sizeof kFxParams / sizeof kFxParams[0]);

// The delay time is tempo-synced, 32 to the beat: straight and dotted note
// lengths fall on whole values.
const std::pair<int, const char*> kNoteLengths[] = {{2, "1/64"},  {4, "1/32"},  {6, "1/32."}, {8, "1/16"},
                                                     {12, "1/16."}, {16, "1/8"},  {24, "1/8."},  {32, "1/4"},
                                                     {48, "1/4."}, {64, "1/2"},  {96, "1/2."}, {127, "1 BAR"}};

const std::vector<int>& snapPoints(const juce::String& bare)
{
    static const std::vector<int> none, delay = [] {
        std::vector<int> v;
        for (auto [at, name] : kNoteLengths)
            v.push_back(at);
        return v;
    }();
    // LFO speed (64 = stopped): even rates either way, which divide with
    // the multipliers (16 x4 = a cycle every 2 bars, 32 x4 = every bar)
    static const std::vector<int> speed{1, 16, 32, 48, 56, 60, 64, 68, 72, 80, 96, 112, 127};
    return bare == "delaytime" ? delay : bare == "lfospeed" ? speed : none;
}

double glideMs(int v) { return std::pow(10000.0, juce::jlimit(0, 127, v) / 127.0); }

juce::String paramId(int track, const char* id)
{
    if (track < 0)
        return id;
    if (track >= kTracks)
        return juce::String("fx_") + id;
    return "t" + juce::String(track + 1) + "_" + id;
}

namespace
{
juce::File stateDir()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Application Support/CyclesEmu/state");
}

const juce::StringArray* choicesFor(const char* id)
{
    if (!std::strcmp(id, "machine")) return &kMachineNames;
    if (!std::strcmp(id, "lfowave")) return &kLfoWaveNames;
    if (!std::strcmp(id, "lforeset")) return &kLfoTrigNames;
    if (!std::strcmp(id, "lfomult")) return &kLfoMultNames;
    if (!std::strcmp(id, "lfodest")) return &kLfoDestNames;
    static const juce::StringArray priority{"Last", "Low", "High"}, slope{"Constant Rate", "Constant Time"},
        style{"Glide", "Glissando"};
    if (!std::strcmp(id, "notepri")) return &priority;
    if (!std::strcmp(id, "ftype")) return &kFilterTypeNames;
    if (!std::strcmp(id, "pslope")) return &slope;
    if (!std::strcmp(id, "pstyle")) return &style;
    return nullptr;
}

const char* const glideIds[] = {"port", "ptim", "pamount", "legato", "notepri", "pslope", "pstyle", "plegonly", "pgating"};

// a glide setting's starting value: a 100 ms time at full amount, the rest off
int glideDefault(const char* id)
{
    return !std::strcmp(id, "ptim") ? 64 : !std::strcmp(id, "pamount") ? 100 : 0;
}

juce::String showValue(Show s, int v)
{
    if (s == Show::NoteLength)
    {
        for (auto [at, name] : kNoteLengths)
            if (v == at)
                return name;
        return juce::String(v);
    }
    if (s == Show::GlideTime)
    {
        const double ms = glideMs(v);
        // compact, for the value box: "104ms", "1.25s", "10.0s"
        return ms < 1000.0 ? juce::String(juce::roundToInt(ms)) + "ms"
                           : juce::String(ms / 1000.0, ms < 10000.0 ? 2 : 1) + "s";
    }
    switch (s)
    {
        case Show::Bipolar: return (v > 64 ? "+" : "") + juce::String(v - 64);
        case Show::Semitones: return (v > 64 ? "+" : "") + juce::String(v - 64) + " st";
        case Show::Pan: return v == 64 ? juce::String("C") : v < 64 ? "L" + juce::String(64 - v) : "R" + juce::String(v - 64);
        case Show::Cutoff: return v == 64 ? juce::String("OFF") : v < 64 ? "L" + juce::String(64 - v) : "H" + juce::String(v - 64);
        default: return juce::String(v);
    }
}

int parseValue(Show s, const juce::String& t)
{
    auto x = t.trim();
    if (s == Show::GlideTime)   // a time ("250 ms", "1.5 s"), or the raw value
    {
        const double n = x.getDoubleValue();
        const double ms = x.endsWithIgnoreCase("ms") ? n : x.endsWithIgnoreCase("s") ? n * 1000.0 : -1.0;
        return ms > 0 ? juce::roundToInt(127.0 * std::log(ms) / std::log(10000.0)) : x.getIntValue();
    }
    if (s == Show::NoteLength)
        for (auto [at, name] : kNoteLengths)
            if (x.equalsIgnoreCase(name))
                return at;
    switch (s)
    {
        case Show::Bipolar:
        case Show::Semitones: return x.getIntValue() + 64;
        case Show::Pan:
        case Show::Cutoff:
            if (x.startsWithIgnoreCase("L")) return 64 - x.substring(1).getIntValue();
            if (x.startsWithIgnoreCase("R") || x.startsWithIgnoreCase("H")) return 64 + x.substring(1).getIntValue();
            return 64;
        default: return x.getIntValue();
    }
}

// The three parameter kinds, with their default asked of the processor when
// the host (or a reset) wants it: the hardware's, which depends on the kit
// and the machine, known only once the engine is loaded.
using DefaultFn = std::function<float()>;
template <typename Base> struct HardwareDefault : Base
{
    template <typename... Args> HardwareDefault(DefaultFn f, Args&&... args) : Base(std::forward<Args>(args)...), def(std::move(f)) {}
    float getDefaultValue() const override   // else the range's start, as declared
    {
        const float v = def ? def() : -1.0f;
        return v >= 0.0f ? this->convertTo0to1(v) : 0.0f;
    }
    DefaultFn def;
};

std::unique_ptr<juce::RangedAudioParameter> makeParam(const ParamDef& d, const juce::String& id,
                                                      const juce::String& name, DefaultFn def)
{
    if (auto* names = choicesFor(d.id))
        return std::make_unique<HardwareDefault<juce::AudioParameterChoice>>(def, juce::ParameterID{id, 1}, name,
                                                                             *names, 0);
    if (d.kind == Kind::LfoSync)
        return std::make_unique<HardwareDefault<juce::AudioParameterChoice>>(
            def, juce::ParameterID{id, 1}, name, juce::StringArray{"Sync", "Free"}, 0);
    if (d.hi == 1)
        return std::make_unique<HardwareDefault<juce::AudioParameterBool>>(def, juce::ParameterID{id, 1}, name, false);
    const Show show = d.show;
    return std::make_unique<HardwareDefault<juce::AudioParameterInt>>(
        def, juce::ParameterID{id, 1}, name, d.lo, d.hi, d.lo,
        juce::AudioParameterIntAttributes()
            .withStringFromValueFunction([show](int v, int) { return showValue(show, v); })
            .withValueFromStringFunction([show](const juce::String& t) { return parseValue(show, t); }));
}
} // namespace
} // namespace cyclesemu

using namespace cyclesemu;

juce::AudioProcessor::BusesProperties CyclesEmuProcessor::busesFor(Product p)
{
    auto b = BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true);
    return p == Product::Fx ? b.withInput("Input", juce::AudioChannelSet::stereo(), true) : b;
}

CyclesEmuProcessor::CyclesEmuProcessor(Product p)
    : AudioProcessor(busesFor(p)), product(p), apvts(*this, nullptr, "params", makeLayout())
{
    int err = 0;
    engine = mc_engine_new(stateDir().getFullPathName().toRawUTF8(), &err);
    if (engine == nullptr)
        engineError = "Engine state not found or unreadable in " + stateDir().getFullPathName()
                      + " (error " + juce::String(err) + "). Run aot/dump_state.py and copy it there.";
    midiMode = apvts.getRawParameterValue("midimode");     // CyclesEmu only
    keysTrack = apvts.getRawParameterValue("keystrack");
    voiceCount = apvts.getRawParameterValue("voices");     // CyclesVoice only
    fxDelaySend = apvts.getRawParameterValue("fxdelay");   // CyclesFX only
    fxReverbSend = apvts.getRawParameterValue("fxreverb");
    fxMix = apvts.getRawParameterValue("fxmix");
    outGain = apvts.getRawParameterValue("outgain");
    tempoParam = apvts.getRawParameterValue("tempo");
    followHost = apvts.getRawParameterValue("followhost");
    if (engine != nullptr)
    {
        mc_engine_set_tempo(engine, 120.0);   // internal clock; a saved state may wait for MIDI clock
        mc_engine_set_smooth_filter(engine, 1);   // the track filter without zipper steps (engine.c)
        appliedBpm = 120.0;
        if (mc_engine_get_param(engine, 6, 0x16) > 126)   // reverb size 127 plays as 126 (step 10)
            mc_engine_param(engine, 6, 0x16, 126);
    }
    for (auto& s : slots)   // first every slot's parameter: applying one may read another
    {
        s.param = apvts.getParameter(s.id);
        s.raw = apvts.getRawParameterValue(s.id);
    }
    for (auto& s : slots)   // then every value as the engine has it, before anything is applied
    {
        int v = engine ? currentEngineValue(s) : s.def->lo;
        s.lastParam = v;
        s.param->setValueNotifyingHost(s.param->convertTo0to1((float)v));
    }
    for (int t = 0; t < trackPages(); ++t)
        for (int i = 0; i < kGlideParams; ++i)
            glideRaw[t][i] = apvts.getRawParameterValue(paramId(t, glideIds[i]));
    for (int t = 0; engine != nullptr && t < kTracks; ++t)   // the pitch the glides add to
    {
        glides[t].userPitch = mc_engine_get_param(engine, t, 0x28);
        glides[t].sentWord = glides[t].userPitch << 8;
    }
    for (int t = 0; engine != nullptr && t < kTracks; ++t)   // the mixer's starting point
    {
        levelNow[t] = levelSent[t] = mc_engine_get_level(engine, t);
        levelEased[t] = (float)levelSent[t] / 127.0f;
        mutedNow[t] = mc_engine_get_mute(engine, t) != 0;
    }
    if (engine != nullptr)
        applyMix();
    if (engine != nullptr && isVoice())   // and one sound on every voice from the start
        for (auto& s : slots)
            applyAll(*s.def, s.track, s.lastParam);
    // what the resets go back to
    initKit = kitToTree(apvts);
    if (auto json = juce::JSON::parse(stateDir().getChildFile("defaults.json")); json.isObject())
    {
        auto* sounds = json["sound"].getDynamicObject();
        haveInitSounds = sounds != nullptr;
        for (int m = 0; haveInitSounds && m < 11; ++m)
        {
            auto snd = (*sounds).getProperty(juce::String(m));
            haveInitSounds = snd.isObject();
            for (int k = 0; haveInitSounds && k < 33; ++k)
                initSound[m][k] = snd[juce::Identifier(juce::String(k))];
        }
    }
}

void CyclesEmuProcessor::resetTrack(int t)
{
    if (!haveInitSounds || t < 0 || t >= kTracks)
        return;
    auto plain = [&](const char* id) {
        auto* p = apvts.getParameter(paramId(t, id));
        return juce::roundToInt(p->convertFrom0to1(p->getValue()));
    };
    const int machine = juce::jlimit(0, 10, plain("machine"));
    const int* k = initSound[machine];
    for (int i = 0; i < kNumTrackParams; ++i)
    {
        const auto& d = kTrackParams[i];
        const juce::String id = paramId(t, d.id);
        switch (d.kind)
        {
            case Kind::Sound: setPlain(apvts, id, (float)k[(d.offset - 0x14) / 2]); break;
            // the firmware's multiplier 0-23: 0-11 synced x1..x2k, 12-23 free
            case Kind::LfoSync: setPlain(apvts, id, k[2] >= 12 ? 1.0f : 0.0f); break;
            case Kind::LfoMult: setPlain(apvts, id, (float)(k[2] % 12)); break;
            case Kind::LfoDest:
                for (int j = 0; j < kNumLfoDests; ++j)
                    if (kLfoDestValues[j] == k[4])
                        setPlain(apvts, id, (float)j);
                break;
            case Kind::Mute:
            case Kind::Solo: setPlain(apvts, id, 0.0f); break;
            case Kind::Level:   // the level a new kit gives this track
                for (auto tr : initKit)
                    if (tr.hasType("Track") && (int)tr.getProperty("index") == t)
                        setPlain(apvts, id, (float)tr.getProperty("level"));
                break;
            case Kind::Glide: setPlain(apvts, id, (float)glideDefault(d.id)); break;
            case Kind::Filter: setPlain(apvts, id, 0.0f); break;
            case Kind::Fx: break;
        }
    }
}

void CyclesEmuProcessor::resetKit() { treeToKit(apvts, initKit); }

float CyclesEmuProcessor::defaultFor(const juce::String& id) const
{
    if (engine == nullptr || !initKit.isValid())
        return -1.0f;   // not loaded yet: the declared default
    // which track (6 = FX) and which parameter
    int t = -1;
    juce::String bare = id;
    if (id.startsWith("fx_"))
        t = kTracks, bare = id.substring(3);
    else if (id.length() > 3 && id[0] == 't' && id[2] == '_')
        t = id[1] - '1', bare = id.substring(3);
    if (t < 0)
        return -1.0f;
    // the new kit's value: the FX, and a track's level
    auto fromKit = [&]() -> float {
        for (auto tr : initKit)
            if ((t == kTracks && tr.hasType("FX")) || (tr.hasType("Track") && (int)tr.getProperty("index") == t))
                return tr.hasProperty(juce::Identifier(bare)) ? (float)tr.getProperty(juce::Identifier(bare)) : -1.0f;
        return -1.0f;
    };
    if (t == kTracks || bare == "level")
        return fromKit();
    if (bare == "mute" || bare == "solo")
        return 0.0f;
    if (!haveInitSounds)
        return fromKit();
    // the init sound of the track's current machine
    const auto* mp = apvts.getParameter(paramId(t, "machine"));
    const int m = mp ? juce::jlimit(0, 10, juce::roundToInt(mp->convertFrom0to1(mp->getValue()))) : 0;
    const int* k = initSound[m];
    for (int i = 0; i < kNumTrackParams; ++i)
        if (bare == kTrackParams[i].id)
        {
            const auto& d = kTrackParams[i];
            switch (d.kind)
            {
                case Kind::Sound: return (float)(d.offset == 0x26 ? m : k[(d.offset - 0x14) / 2]);   // machine: stays
                case Kind::LfoSync: return k[2] >= 12 ? 1.0f : 0.0f;
                case Kind::LfoMult: return (float)(k[2] % 12);
                case Kind::LfoDest:
                    for (int j = 0; j < kNumLfoDests; ++j)
                        if (kLfoDestValues[j] == k[4])
                            return (float)j;
                    return 0.0f;
                case Kind::Glide: return (float)glideDefault(d.id);
                case Kind::Filter: return 0.0f;
                case Kind::Level:
                case Kind::Mute:
                case Kind::Solo:
                case Kind::Fx: break;
            }
        }
    return -1.0f;
}

CyclesEmuProcessor::~CyclesEmuProcessor() { mc_engine_free(engine); }

juce::AudioProcessorValueTreeState::ParameterLayout CyclesEmuProcessor::makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    // CyclesVoice: track 1's parameters are the sound, played on all six
    for (int t = 0; t < trackPages(); ++t)
    {
        auto group = isVoice() ? std::make_unique<juce::AudioProcessorParameterGroup>("t1", "Voice", "|")
                               : std::make_unique<juce::AudioProcessorParameterGroup>(
                                     "t" + juce::String(t + 1), "Track " + juce::String(t + 1), "|");
        for (int i = 0; i < kNumTrackParams; ++i)
        {
            const auto& d = kTrackParams[i];
            juce::String id = paramId(t, d.id);
            group->addChild(makeParam(d, id, isVoice() ? juce::String(d.name) : "T" + juce::String(t + 1) + " " + d.name,
                                      [this, id] { return defaultFor(id); }));
            slots.push_back({id, &d, t});
        }
        layout.add(std::move(group));
    }
    if (isVoice())
        layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"voices", 1}, "Voices", 1, kTracks, kTracks));
    else if (isFx())
    {
        // how much of the input each effect gets, and the dry/wet balance
        layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"fxdelay", 1}, "Delay Send", 0, 127, 80));
        layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"fxreverb", 1}, "Reverb Send", 0, 127, 80));
        layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"fxmix", 1}, "Mix", 0, 100, 100,
                                                             juce::AudioParameterIntAttributes().withLabel("%")));
    }
    else
    {
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{"midimode", 1}, "MIDI Mode",
            juce::StringArray{"Drum Map C1-F1", "Ch 1-6 = Tracks"}, 0));
        layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"keystrack", 1}, "Keys Track", 1, 6, 1));
    }
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"followhost", 1}, "Follow Host Tempo", true));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"tempo", 1}, "Tempo",
        juce::NormalisableRange<float>(30.0f, 300.0f, 0.01f), 120.0f,
        juce::AudioParameterFloatAttributes().withLabel("BPM")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"outgain", 1}, "Output Gain",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));
    auto fx = std::make_unique<juce::AudioProcessorParameterGroup>("fx", "FX", "|");
    for (int i = 0; i < kNumFxParams; ++i)
    {
        const auto& d = kFxParams[i];
        juce::String id = paramId(kTracks, d.id);
        fx->addChild(makeParam(d, id, d.name, [this, id] { return defaultFor(id); }));
        slots.push_back({id, &d, kTracks});
    }
    layout.add(std::move(fx));
    return layout;
}

int CyclesEmuProcessor::currentEngineValue(const Slot& s) const
{
    switch (s.def->kind)
    {
        case Kind::Level: return mc_engine_get_level(engine, s.track);
        case Kind::Mute: return mc_engine_get_mute(engine, s.track);
        case Kind::Solo: return 0;
        case Kind::Glide: return glideDefault(s.def->id);
        case Kind::Filter: return 0;   // the Cycles' filter, as the firmware has it
        case Kind::LfoMult: return juce::jlimit(0, 23, mc_engine_get_param(engine, s.track, 0x18)) % 12;
        case Kind::LfoSync: return mc_engine_get_param(engine, s.track, 0x18) >= 12 ? 1 : 0;
        case Kind::LfoDest:
        {
            int v = mc_engine_get_param(engine, s.track, 0x1c);
            for (int i = 0; i < kNumLfoDests; ++i)
                if (kLfoDestValues[i] == v)
                    return i;
            return 0;
        }
        default: return juce::jlimit(s.def->lo, s.def->hi, mc_engine_get_param(engine, s.track, s.def->offset));
    }
}

int CyclesEmuProcessor::paramValue(int track, const char* id) const
{
    for (const auto& s : slots)
        if (s.track == track && !std::strcmp(s.def->id, id))
            return juce::roundToInt(s.param->convertFrom0to1(s.param->getValue()));
    return 0;
}

// Each track's engine level: its Level, or 0 while muted or while another
// track is soloed and it is not. Only changes reach the engine.
void CyclesEmuProcessor::applyMix()
{
    bool anySolo = false;
    for (bool s : soloNow)
        anySolo |= s;
    for (int t = 0; t < kTracks; ++t)
    {
        const int want = mutedNow[t] || (anySolo && !soloNow[t]) ? 0 : levelNow[t];
        if (want != levelSent[t])
        {
            mc_engine_level(engine, t, want);
            levelSent[t] = want;
        }
    }
}

void CyclesEmuProcessor::applyAll(const ParamDef& d, int track, int v)
{
    if (!isVoice() || track >= kTracks)
        return applyToEngine(d, track, v);
    for (int t = 0; t < kTracks; ++t)
        applyToEngine(d, t, v);
}

void CyclesEmuProcessor::voiceNote(int note, int velocity, bool on)
{
    if (on)
        countTrig(0);
    const auto gs = glideSettings(0);
    // Legato: the sound plays mono, on the first voice. A key is released
    // wherever it plays, should Legato have changed while it was down.
    auto& mono = glides[0];
    if (on && gs.legato)
        return trackNote(0, note, velocity, on);
    const int n = juce::jlimit(1, kTracks, voiceCount != nullptr ? juce::roundToInt(voiceCount->load()) : kTracks);
    if (!on)
    {
        if (std::find(mono.held, mono.held + mono.numHeld, note) != mono.held + mono.numHeld)
            trackNote(0, note, velocity, on);
        for (int t = 0; t < kTracks; ++t)
            if (voices[t].on && voices[t].note == note)
            {
                mc_engine_note(engine, t, note, 0, 0, 0x0681);
                voices[t].on = false;
                voices[t].age = ++voiceClock;
                glides[t].frozen = gs.gating;
            }
        return;
    }
    // the voice released longest ago; with none free, the oldest playing
    int pick = -1;
    bool anyOn = false;
    for (int t = 0; t < n; ++t)
        anyOn |= voices[t].on;
    for (int pass = 0; pass < 2 && pick < 0; ++pass)
        for (int t = 0; t < n; ++t)
            if ((pass == 1 || !voices[t].on) && (pick < 0 || voices[t].age < voices[pick].age))
                pick = t;
    if (voices[pick].on)
        mc_engine_note(engine, pick, voices[pick].note, 0, 0, 0x0681);
    voices[pick] = {note, true, ++voiceClock};
    // portamento: from the pitch the last note played had reached, on whichever voice
    const bool glide = gs.port && lastVoice >= 0 && (!gs.legatoOnly || anyOn);
    startNote(pick, note, velocity, glide, lastVoice >= 0 ? glides[lastVoice].cur : 0.0, gs);
    lastVoice = pick;
}

CyclesEmuProcessor::GlideSettings CyclesEmuProcessor::glideSettings(int page) const
{
    auto v = [&](int i) {
        auto* p = glideRaw[page][i];
        return p != nullptr ? juce::roundToInt(p->load(std::memory_order_relaxed)) : glideDefault(glideIds[i]);
    };
    return {v(0) != 0, v(3) != 0, v(5) != 0, v(6) != 0, v(7) != 0, v(8) != 0, v(4), glideMs(v(1)), v(2) / 100.0};
}

// The glide starts `amount` of the way back toward where it came from, and
// moves at a constant rate (an octave in the time) or takes the time whatever
// the distance.
void CyclesEmuProcessor::beginGlide(Glide& g, double from, const GlideSettings& s)
{
    g.cur = g.target + (from - g.target) * s.amount;
    const double blocks = std::max(1.0, s.ms * 48.0 / kBlock);
    g.step = s.constTime ? std::abs(g.cur - g.target) / blocks : 12.0 / blocks;
    g.frozen = false;
    g.glissando = s.glissando;
}

void CyclesEmuProcessor::startNote(int track, int note, int velocity, bool glide, double from, const GlideSettings& s)
{
    auto& g = glides[track];
    g.trig = note;
    g.velocity = velocity;
    g.target = note;
    if (glide)
        beginGlide(g, from, s);
    else
        g.cur = note, g.step = 0;
    // first: the note starts where the glide does, without the firmware's
    // smoothing sliding there from the last note's pitch
    writePitch(track);
    mc_engine_snap_param(engine, track, 0x28);
    mc_engine_note(engine, track, note, velocity, 1, 0x0681);
}

// Legato: to another key without a new trigger, the pitch word carrying the
// difference; beyond the pitch's +-24 semitones, a new trigger after all.
void CyclesEmuProcessor::legatoTo(int track, int note, const GlideSettings& s)
{
    auto& g = glides[track];
    const int total = g.userPitch + note - g.trig;
    if (total < 40 || total > 88)
        return startNote(track, note, g.velocity, s.port, g.cur, s);
    g.target = note;
    if (s.port)
        beginGlide(g, g.cur, s);
    else
        g.cur = note, g.step = 0;
    writePitch(track);
    if (!s.port)   // a legato step without portamento: at once, as on the hardware
        mc_engine_snap_param(engine, track, 0x28);
}

// A track's notes: in Legato, a key pressed while another is held moves the
// pitch (to the key Note Priority picks) instead of triggering, and releasing
// it goes back to the one held; otherwise every key triggers, gliding from
// the last note when Portamento is on (Legato Only: only from a held one).
void CyclesEmuProcessor::trackNote(int track, int note, int velocity, bool on)
{
    if (on && !isVoice())   // (CyclesVoice counted in voiceNote)
        countTrig(track);
    const auto s = glideSettings(isVoice() ? 0 : track);
    auto& g = glides[track];
    const bool anyHeld = g.numHeld > 0;
    g.numHeld = (int)(std::remove(g.held, g.held + g.numHeld, note) - g.held);
    auto priorityNote = [&] {
        if (s.priority == 1)
            return *std::min_element(g.held, g.held + g.numHeld);
        if (s.priority == 2)
            return *std::max_element(g.held, g.held + g.numHeld);
        return g.held[g.numHeld - 1];
    };
    if (on)
    {
        if (g.numHeld == (int)std::size(g.held))   // full: forget the oldest
            std::copy(g.held + 1, g.held + g.numHeld--, g.held);
        g.held[g.numHeld++] = note;
        if (s.legato && anyHeld && g.trig >= 0)
        {
            if (const int want = priorityNote(); want != (int)g.target)
                legatoTo(track, want, s);
            return;
        }
        return startNote(track, note, velocity, s.port && g.trig >= 0 && (!s.legatoOnly || anyHeld), g.cur, s);
    }
    if (s.legato && g.numHeld > 0)
    {
        if (const int want = priorityNote(); want != (int)g.target)
            legatoTo(track, want, s);
        return;
    }
    if (s.legato && !anyHeld)
        return;   // not a key it knew of
    mc_engine_note(engine, track, s.legato ? g.trig : note, 0, 0, 0x0681);
    if (s.gating && (s.legato || note == g.trig))   // Gating: the glide stops with the key
        g.frozen = true;
}

// The track's pitch word: its Pitch, plus where the glide is relative to the
// note the engine was triggered with (Glissando: in whole semitones).
void CyclesEmuProcessor::writePitch(int track, bool force)
{
    auto& g = glides[track];
    const double at = g.glissando && g.step > 0 ? std::round(g.cur) : g.cur;
    const int offset = g.trig >= 0 ? juce::roundToInt((at - g.trig) * 256.0) : 0;
    const int word = juce::jlimit(40 << 8, 88 << 8, (g.userPitch << 8) + offset);
    if (word != g.sentWord || force)
    {
        mc_engine_param_word(engine, track, 0x28, word);
        g.sentWord = word;
    }
}

void CyclesEmuProcessor::updateGlides()
{
    for (int t = 0; t < kTracks; ++t)
    {
        auto& g = glides[t];
        if (g.step <= 0 || g.frozen)
            continue;
        const double d = g.target - g.cur;
        if (std::abs(d) <= g.step)
            g.cur = g.target, g.step = 0;
        else
            g.cur += d > 0 ? g.step : -g.step;
        writePitch(t);
    }
}

void CyclesEmuProcessor::applyLfoMult(int track)
{
    const int pt = isVoice() ? 0 : track;   // CyclesVoice: the one sound's settings
    int mult = juce::jlimit(0, 11, paramValue(pt, "lfomult"));
    int freeRunning = paramValue(pt, "lfosync") != 0;
    mc_engine_param(engine, track, 0x18, mult + 12 * freeRunning);
}

void CyclesEmuProcessor::applyToEngine(const ParamDef& d, int track, int v)
{
    v = juce::jlimit(d.lo, d.hi, v);
    // Interim (NOTES.md step 10): as emulated, reverb size 127 never decays,
    // while the hardware's does; play it as 126 until the arithmetic
    // difference is found.
    if (d.kind == Kind::Fx && d.offset == 0x16 && v > 126)
        v = 126;
    switch (d.kind)
    {
        case Kind::Sound:
            if (d.offset == 0x28)   // pitch: with the glide's offset
            {
                glides[track].userPitch = v;
                writePitch(track, true);
            }
            else
                mc_engine_param(engine, track, d.offset, v);
            break;
        case Kind::Glide: break;   // read at each note
        case Kind::Filter:
            (std::strcmp(d.id, "ftype") ? filterExtra : filterType)[track] = v;
            mc_engine_set_filter(engine, track, filterEngineType(filterType[track]), filterExtra[track]);
            break;
        case Kind::Fx: mc_engine_param(engine, 6, d.offset, v); break;
        case Kind::Level:
            levelNow[track] = v;
            applyMix();
            break;
        case Kind::Mute:
            mutedNow[track] = v != 0;
            mc_engine_mute(engine, track, v != 0);
            applyMix();
            break;
        case Kind::Solo:
            soloNow[track] = v != 0;
            applyMix();
            break;
        case Kind::LfoMult:
        case Kind::LfoSync: applyLfoMult(track); break;
        case Kind::LfoDest: mc_engine_param(engine, track, 0x1c, kLfoDestValues[v]); break;
    }
}

// Host parameter changes since the last block: applied only when the
// parameter itself moved, so a CC that set the engine is not undone.
void CyclesEmuProcessor::applyParameterChanges()
{
    for (auto& s : slots)
    {
        int v = juce::roundToInt(s.raw->load(std::memory_order_relaxed));
        if (v != s.lastParam)
        {
            s.lastParam = v;
            applyAll(*s.def, s.track, v);
        }
    }
}

void CyclesEmuProcessor::handleMidi(const juce::MidiMessage& m)
{
    int ch = m.getChannel() - 1;
    if (isVoice())
    {
        // one sound: notes on any channel to the voices, CCs to every voice
        if (m.isNoteOnOrOff())
            return voiceNote(m.getNoteNumber(), m.getVelocity(), m.isNoteOn());
        if (!m.isController())
            return;
        ch = 0;
    }
    if (m.isNoteOnOrOff())
    {
        int track = ch, note = m.getNoteNumber();
        if (midiMode->load() < 0.5f)              // Drum Map
        {
            if (note >= 36 && note <= 41)
            {
                track = note - 36;
                note = 60;                        // the track's programmed pitch
            }
            else if (note >= 48)
                track = juce::jlimit(1, kTracks, (int)keysTrack->load()) - 1;
            else
                return;
        }
        if (track < 0 || track >= kTracks)
            return;
        trackNote(track, note, m.getVelocity(), m.isNoteOn());
        return;
    }
    if (ch < 0 || ch >= kTracks || !m.isController())
        return;
    int cc = m.getControllerNumber(), v = m.getControllerValue();
    const int first = ch, last = isVoice() ? kTracks - 1 : ch;   // CyclesVoice: every voice
    if (cc == 103)                                // multiplier: the firmware's 0-23 directly
    {
        for (int t = first; t <= last; ++t)
            mc_engine_param(engine, t, 0x18, juce::jlimit(0, 23, v));
        return;
    }
    if (cc == 105)                                // destination: the firmware takes 0, 10-22 and 28-31
    {
        if (v == 0 || (v >= 10 && v <= 22) || (v >= 28 && v <= 31))
            for (int t = first; t <= last; ++t)
                mc_engine_param(engine, t, 0x1c, v);
        return;
    }
    for (int i = 0; i < kNumTrackParams; ++i)
    {
        const auto& d = kTrackParams[i];
        if (d.cc == cc)
        {
            // punch and gate: the firmware stored 1 for every non-zero value tried
            applyAll(d, ch, d.hi == 1 ? (v != 0) : v);
            return;
        }
    }
    for (int i = 0; i < kNumFxParams; ++i)
        if (kFxParams[i].cc == cc)
        {
            applyToEngine(kFxParams[i], 6, v);
            return;
        }
}

void CyclesEmuProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    resample = std::abs(sampleRate - 48000.0) > 0.5;
    ratio = 48000.0 / sampleRate;
    if (resample)
        resampler.prepare(48000.0, sampleRate);
    dcL.reset();
    dcR.reset();
    srcL.clear();
    srcR.clear();
    // room for the largest call, so the audio thread never reallocates
    const size_t room = (size_t)std::ceil(juce::jmax(samplesPerBlock, 1024) * 48000.0 / sampleRate)
                        + Resampler::kTaps + 2 * kBlock;
    srcL.reserve(room);
    srcR.reserve(room);
    carry.ensureSize(4096);
    blockPos = kBlock;
    // The latency is set once, with its final value: each change makes the
    // host restart the plugin (Live: setActive -> prepareToPlay), so setting
    // a value and then another, as CyclesFX did (0, then 32), looped forever
    // and hung Live's main thread (NOTES.md step 34).
    int latency = resample ? juce::roundToInt(Resampler::latency() / ratio) : 0;
    if (isFx())
    {
        std::fill(std::begin(inCollectL), std::end(inCollectL), 0.0f);
        std::fill(std::begin(inCollectR), std::end(inCollectR), 0.0f);
        std::fill(std::begin(blockL), std::end(blockL), 0.0f);
        std::fill(std::begin(blockR), std::end(blockR), 0.0f);
        if (!resample)
            latency = kBlock;   // a block in, a block out
        else
        {
            // Both sides start with silence: the output (48 kHz) enough for its
            // resampler's lookahead, the input (host rate) enough that a 48 kHz
            // block can always be made when the output asks, so neither ever
            // runs dry and the timing is fixed.
            inResampler.prepare(sampleRate, 48000.0);
            const int outAhead = Resampler::kTaps + 2 * kBlock, inAhead = Resampler::kTaps + 4 * kBlock;
            srcL.assign((size_t)outAhead, 0.0f);
            srcR.assign((size_t)outAhead, 0.0f);
            inFifoL.assign((size_t)inAhead, 0.0f);
            inFifoR.assign((size_t)inAhead, 0.0f);
            inFifoL.reserve(room * 2 + (size_t)inAhead);
            inFifoR.reserve(room * 2 + (size_t)inAhead);
            // Host output k reads 48 kHz output k*48k/rate + D, which is engine
            // output (that - outAhead), made from the same 48 kHz input frame
            // (a block's input is its own here), which the input resampler made
            // from host input (that)*rate/48k + D - inAhead: the latency is
            // inAhead - D + (outAhead - D)*rate/48k.
            const double d = Resampler::latency();
            latency = juce::roundToInt(inAhead - d + (outAhead - d) / ratio);
        }
    }
    setLatencySamples(latency);
}

bool CyclesEmuProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;
    return !isFx() || layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
}

// CyclesFX, one engine block: 32 input frames into the sends, 32 out, with
// the dry input (as long delayed) mixed in by Mix.
void CyclesEmuProcessor::fxEngineBlock(const float* inL, const float* inR, float* outL, float* outR)
{
    // the send knobs' law: square, so the knob's middle is about -12 dB
    auto amount = [](std::atomic<float>* p) {
        const float v = juce::jlimit(0.0f, 127.0f, p->load()) / 127.0f;
        return v * v;
    };
    std::copy(inL, inL + kBlock, inBlockL);
    std::copy(inR, inR + kBlock, inBlockR);
    mc_engine_set_input(engine, inBlockL, inBlockR, amount(fxDelaySend), amount(fxReverbSend));
    renderEngineBlock();   // into blockL/R (the wet), DC blocker and output gain included
    const float wet = juce::jlimit(0.0f, 100.0f, fxMix->load()) / 100.0f, dry = 1.0f - wet;
    for (int i = 0; i < kBlock; ++i)
    {
        outL[i] = wet * blockL[i] + dry * inBlockL[i];
        outR[i] = wet * blockR[i] + dry * inBlockR[i];
    }
}

// CyclesFX: at 48 kHz, each engine block takes the 32 input frames that came
// in while the previous block played (32 frames of latency). At other rates,
// the input is resampled to 48 kHz, run through the same blocks, and the
// result resampled back.
void CyclesEmuProcessor::fxProcess(juce::AudioBuffer<float>& buffer)
{
    const int n = buffer.getNumSamples();
    const float* inL = buffer.getReadPointer(0);
    const float* inR = buffer.getNumChannels() > 1 ? buffer.getReadPointer(1) : inL;
    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getWritePointer(1);
    if (!resample)
    {
        for (int i = 0; i < n; ++i)
        {
            if (blockPos == kBlock)
            {
                float oL[kBlock], oR[kBlock];
                fxEngineBlock(inCollectL, inCollectR, oL, oR);
                std::copy(oL, oL + kBlock, blockL);
                std::copy(oR, oR + kBlock, blockR);
                blockPos = 0;
            }
            const float l = inL[i], r = inR[i];   // read before the output overwrites them
            outL[i] = blockL[blockPos];
            outR[i] = blockR[blockPos];
            inCollectL[blockPos] = l;
            inCollectR[blockPos] = r;
            ++blockPos;
        }
        return;
    }
    inFifoL.insert(inFifoL.end(), inL, inL + n);
    inFifoR.insert(inFifoR.end(), inR, inR + n);
    const int needed = resampler.needed(n);
    while ((int)srcL.size() < needed)
    {
        float in48L[kBlock] = {}, in48R[kBlock] = {}, oL[kBlock], oR[kBlock];
        if ((int)inFifoL.size() >= inResampler.needed(kBlock))   // else a gap of silence
        {
            const int used = inResampler.process(inFifoL.data(), inFifoR.data(), in48L, in48R, kBlock);
            inFifoL.erase(inFifoL.begin(), inFifoL.begin() + used);
            inFifoR.erase(inFifoR.begin(), inFifoR.begin() + used);
        }
        fxEngineBlock(in48L, in48R, oL, oR);
        srcL.insert(srcL.end(), oL, oL + kBlock);
        srcR.insert(srcR.end(), oR, oR + kBlock);
    }
    const int used = resampler.process(srcL.data(), srcR.data(), outL, outR, n);
    srcL.erase(srcL.begin(), srcL.begin() + used);
    srcR.erase(srcR.begin(), srcR.begin() + used);
}

void CyclesEmuProcessor::renderEngineBlock()
{
    applyParameterChanges();
    updateGlides();
    if (std::abs(targetBpm - appliedBpm) > 1e-4)
    {
        mc_engine_set_tempo(engine, targetBpm);
        appliedBpm = targetBpm;
    }
    const int renderStatus = mc_engine_render(engine, blockL, blockR);
    // the mixer's meters: each track's peak in the render's own track
    // buffers (SRAM 0x80001858 + 128 t: 32 big-endian Q31 samples, after
    // Vol+Dist, before level and pan), times its level's gain
    {
        uint8_t raw[kTracks * 128];
        mc_engine_read(engine, 0x80001858u, raw, sizeof raw);
        for (int t = 0; t < kTracks; ++t)
        {
            int32_t peak = 0;
            for (int i = 0; i < 32; ++i)
            {
                const uint8_t* b = raw + 128 * t + 4 * i;
                const int32_t s = (int32_t)((uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3]);
                peak = std::max(peak, s == INT32_MIN ? INT32_MAX : std::abs(s));
            }
            // post-fader: the firmware's level gain is (level / 127)^2
            // (measured: output exactly proportional to level squared), the
            // level eased toward levelSent (0 while muted or soloed out) by
            // 1 - 0.9696 a block, as the firmware does (measured from the
            // output after level steps 127 -> 64, 0, 100)
            levelEased[t] += ((float)juce::jmax(0, levelSent[t]) / 127.0f - levelEased[t]) * (1.0f - 0.9696f);
            const float lv = levelEased[t];
            const float p = (float)peak / 2147483648.0f * lv * lv;
            float prev = trackPeaks[t].load(std::memory_order_relaxed);
            while (p > prev && !trackPeaks[t].compare_exchange_weak(prev, p, std::memory_order_relaxed))
            {
            }
        }
    }
    if (renderStatus != 0)
    {
        std::fill(std::begin(blockL), std::end(blockL), 0.0f);
        std::fill(std::begin(blockR), std::end(blockR), 0.0f);
        ++engineFaults;
    }
    dcL.process(blockL, kBlock);
    dcR.process(blockR, kBlock);
    const float g = juce::Decibels::decibelsToGain(outGain->load());
    if (g != 1.0f)
        for (int i = 0; i < kBlock; ++i)
        {
            blockL[i] *= g;
            blockR[i] *= g;
        }
    blockPos = 0;
}

void CyclesEmuProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (engine == nullptr)
    {
        buffer.clear();
        return;
    }
    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getWritePointer(1);

    targetBpm = tempoParam->load();
    if (followHost->load() >= 0.5f)
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (auto bpm = pos->getBpm())
                    targetBpm = juce::jlimit(30.0, 300.0, *bpm);
    shownBpm.store(targetBpm);
    if (isFx())
    {
        fxProcess(buffer);
        outPeak.store(buffer.getMagnitude(0, n), std::memory_order_relaxed);
        return;
    }

    if (!resample)
    {
        // Events take effect at the first engine block that starts at or after them.
        auto it = midi.cbegin();
        for (int i = 0; i < n; ++i)
        {
            if (blockPos == kBlock)
            {
                for (const auto meta : carry)     // due after the previous call's last boundary
                    handleMidi(meta.getMessage());
                carry.clear();
                for (; it != midi.cend() && (*it).samplePosition <= i; ++it)
                    handleMidi((*it).getMessage());
                renderEngineBlock();
            }
            outL[i] = blockL[blockPos];
            outR[i] = blockR[blockPos];
            ++blockPos;
        }
        for (; it != midi.cend(); ++it)   // due after the last boundary: the next one
            carry.addEvent((*it).getMessage(), 0);
        outPeak.store(buffer.getMagnitude(0, n), std::memory_order_relaxed);
        return;
    }

    // Resampled: this call's events go in at its start, then enough 48 kHz
    // engine output is produced to interpolate n host samples.
    for (const auto meta : midi)
        handleMidi(meta.getMessage());
    const int needed = resampler.needed(n);
    while ((int)srcL.size() < needed)
    {
        renderEngineBlock();
        srcL.insert(srcL.end(), blockL, blockL + kBlock);
        srcR.insert(srcR.end(), blockR, blockR + kBlock);
    }
    const int used = resampler.process(srcL.data(), srcR.data(), outL, outR, n);
    srcL.erase(srcL.begin(), srcL.begin() + used);
    srcR.erase(srcR.begin(), srcR.begin() + used);
    outPeak.store(buffer.getMagnitude(0, n), std::memory_order_relaxed);
}

juce::AudioProcessorEditor* CyclesEmuProcessor::createEditor() { return new CyclesEmuEditor(*this); }

void CyclesEmuProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, dest);
}

void CyclesEmuProcessor::setStateInformation(const void* data, int size)
{
    if (auto xml = getXmlFromBinary(data, size))
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CyclesEmuProcessor(); }
