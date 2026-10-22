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
    {"lfowave", "LFO Waveform", 0x1e, 0, 6, 106, Kind::Sound, Show::Plain},
    {"lfosync", "LFO Sync", 0x18, 0, 1, -1, Kind::LfoSync, Show::Plain},
    {"lfomult", "LFO Multiplier", 0x18, 0, 11, 103, Kind::LfoMult, Show::Plain},
    {"lfodest", "LFO Destination", 0x1c, 0, 13, 105, Kind::LfoDest, Show::Plain},
    {"lforeset", "LFO Trig Mode", 0x22, 0, 4, 108, Kind::Sound, Show::Plain},
    {"lfospeed", "LFO Speed", 0x16, 0, 127, 102, Kind::Sound, Show::Bipolar},
    {"lfodepth", "LFO Depth", 0x24, 0, 127, 109, Kind::Sound, Show::Bipolar},
    {"lfofade", "LFO Fade", 0x1a, 0, 127, 104, Kind::Sound, Show::Bipolar},
    {"lfophase", "LFO Start Phase", 0x20, 0, 127, 107, Kind::Sound, Show::Plain},
    // the per-track filter (k 28-31): cutoff L64..OFF..H63, reso, envelope
    // amount (64 = none) and decay; CC 74 and 71 as on the Model:Samples
    {"fcut", "Filter Cutoff", 0x4c, 0, 127, 74, Kind::Sound, Show::Cutoff},
    {"freso", "Filter Reso", 0x4e, 0, 127, 71, Kind::Sound, Show::Plain},
    {"fenv", "Filter Env", 0x50, 0, 127, -1, Kind::Sound, Show::Bipolar},
    {"fdec", "Filter Env Decay", 0x52, 0, 127, -1, Kind::Sound, Show::Plain},
};
const int kNumTrackParams = (int)(sizeof kTrackParams / sizeof kTrackParams[0]);

const ParamDef kFxParams[] = {
    {"delaytime", "Delay Time", 0x12, 0, 127, 85, Kind::Fx, Show::Plain},
    {"delayfeedback", "Delay Feedback", 0x14, 0, 127, 86, Kind::Fx, Show::Plain},
    {"reverbsize", "Reverb Size", 0x16, 0, 127, 87, Kind::Fx, Show::Plain},
    {"reverbtone", "Reverb Tone", 0x18, 0, 127, 88, Kind::Fx, Show::Plain},
};
const int kNumFxParams = (int)(sizeof kFxParams / sizeof kFxParams[0]);

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
    return nullptr;
}

juce::String showValue(Show s, int v)
{
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

std::unique_ptr<juce::RangedAudioParameter> makeParam(const ParamDef& d, const juce::String& id,
                                                      const juce::String& name)
{
    if (auto* names = choicesFor(d.id))
        return std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id, 1}, name, *names, 0);
    if (d.kind == Kind::LfoSync)
        return std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{id, 1}, name,
                                                            juce::StringArray{"Sync", "Free"}, 0);
    if (d.hi == 1)
        return std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id, 1}, name, false);
    const Show show = d.show;
    return std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{id, 1}, name, d.lo, d.hi, d.lo,
        juce::AudioParameterIntAttributes()
            .withStringFromValueFunction([show](int v, int) { return showValue(show, v); })
            .withValueFromStringFunction([show](const juce::String& t) { return parseValue(show, t); }));
}
} // namespace
} // namespace cyclesemu

using namespace cyclesemu;

CyclesEmuProcessor::CyclesEmuProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "params", makeLayout())
{
    int err = 0;
    engine = mc_engine_new(stateDir().getFullPathName().toRawUTF8(), &err);
    if (engine == nullptr)
        engineError = "Engine state not found or unreadable in " + stateDir().getFullPathName()
                      + " (error " + juce::String(err) + "). Run aot/dump_state.py and copy it there.";
    midiMode = apvts.getRawParameterValue("midimode");
    keysTrack = apvts.getRawParameterValue("keystrack");
    outGain = apvts.getRawParameterValue("outgain");
    tempoParam = apvts.getRawParameterValue("tempo");
    followHost = apvts.getRawParameterValue("followhost");
    if (engine != nullptr)
    {
        mc_engine_set_tempo(engine, 120.0);   // internal clock; a saved state may wait for MIDI clock
        appliedBpm = 120.0;
        if (mc_engine_get_param(engine, 6, 0x16) > 126)   // reverb size 127 plays as 126 (step 10)
            mc_engine_param(engine, 6, 0x16, 126);
    }
    for (auto& s : slots)
    {
        s.param = apvts.getParameter(s.id);
        s.raw = apvts.getRawParameterValue(s.id);
        int v = engine ? currentEngineValue(s) : s.def->lo;
        s.lastParam = v;
        s.param->setValueNotifyingHost(s.param->convertTo0to1((float)v));
    }
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
            case Kind::Mute: setPlain(apvts, id, 0.0f); break;
            case Kind::Level:   // the level a new kit gives this track
                for (auto tr : initKit)
                    if (tr.hasType("Track") && (int)tr.getProperty("index") == t)
                        setPlain(apvts, id, (float)tr.getProperty("level"));
                break;
            case Kind::Fx: break;
        }
    }
}

void CyclesEmuProcessor::resetKit() { treeToKit(apvts, initKit); }

CyclesEmuProcessor::~CyclesEmuProcessor() { mc_engine_free(engine); }

juce::AudioProcessorValueTreeState::ParameterLayout CyclesEmuProcessor::makeLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int t = 0; t < kTracks; ++t)
    {
        auto group = std::make_unique<juce::AudioProcessorParameterGroup>(
            "t" + juce::String(t + 1), "Track " + juce::String(t + 1), "|");
        for (int i = 0; i < kNumTrackParams; ++i)
        {
            const auto& d = kTrackParams[i];
            juce::String id = paramId(t, d.id);
            group->addChild(makeParam(d, id, "T" + juce::String(t + 1) + " " + d.name));
            slots.push_back({id, &d, t});
        }
        layout.add(std::move(group));
    }
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"midimode", 1}, "MIDI Mode",
        juce::StringArray{"Drum Map C1-F1", "Ch 1-6 = Tracks"}, 0));
    layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"keystrack", 1}, "Keys Track", 1, 6, 1));
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
        fx->addChild(makeParam(d, id, d.name));
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

void CyclesEmuProcessor::applyLfoMult(int track)
{
    int mult = juce::jlimit(0, 11, paramValue(track, "lfomult"));
    int freeRunning = paramValue(track, "lfosync") != 0;
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
        case Kind::Sound: mc_engine_param(engine, track, d.offset, v); break;
        case Kind::Fx: mc_engine_param(engine, 6, d.offset, v); break;
        case Kind::Level: mc_engine_level(engine, track, v); break;
        case Kind::Mute: mc_engine_mute(engine, track, v != 0); break;
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
            applyToEngine(*s.def, s.track, v);
        }
    }
}

void CyclesEmuProcessor::handleMidi(const juce::MidiMessage& m)
{
    int ch = m.getChannel() - 1;
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
        mc_engine_note(engine, track, note, m.isNoteOn() ? m.getVelocity() : 0, m.isNoteOn() ? 1 : 0, 0x0681);
        return;
    }
    if (ch < 0 || ch >= kTracks || !m.isController())
        return;
    int cc = m.getControllerNumber(), v = m.getControllerValue();
    if (cc == 103)                                // multiplier: the firmware's 0-23 directly
    {
        mc_engine_param(engine, ch, 0x18, juce::jlimit(0, 23, v));
        return;
    }
    if (cc == 105)                                // destination: the firmware takes 0, 10-22 and 28-31
    {
        if (v == 0 || (v >= 10 && v <= 22) || (v >= 28 && v <= 31))
            mc_engine_param(engine, ch, 0x1c, v);
        return;
    }
    for (int i = 0; i < kNumTrackParams; ++i)
    {
        const auto& d = kTrackParams[i];
        if (d.cc == cc)
        {
            // punch and gate: the firmware stored 1 for every non-zero value tried
            applyToEngine(d, ch, d.hi == 1 ? (v != 0) : v);
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
    setLatencySamples(resample ? juce::roundToInt(Resampler::latency() / ratio) : 0);
}

bool CyclesEmuProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void CyclesEmuProcessor::renderEngineBlock()
{
    applyParameterChanges();
    if (std::abs(targetBpm - appliedBpm) > 1e-4)
    {
        mc_engine_set_tempo(engine, targetBpm);
        appliedBpm = targetBpm;
    }
    if (mc_engine_render(engine, blockL, blockR) != 0)
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
