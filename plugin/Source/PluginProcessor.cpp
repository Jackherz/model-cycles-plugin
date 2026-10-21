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
// firmware clamps them; FX CCs work on any of those channels. Every sound and FX parameter is also a host parameter, initialised
// from the loaded kit. The engine renders 32-frame blocks at 48 kHz; events
// take effect at the next block boundary, as on the hardware's control
// rate. At other host rates the 48 kHz stream is resampled.

#include <juce_audio_utils/juce_audio_utils.h>
#include <cstring>
#include "engine.h"
#include "DcBlock.h"

namespace
{
constexpr int kTracks = 6;
constexpr int kBlock = 32;
constexpr double kEngineRate = 48000.0;
constexpr uint32_t kTrigFlags = 0x0681;   // the factory pattern's per-track trig flags (NOTES.md)

enum class Kind { Sound, Fx, Level, Mute };

struct ParamDef
{
    const char* id;
    const char* name;
    int offset;     // record offset (sound) or FX offset
    int lo, hi;
    int cc;         // the CC that sets it, -1 if none
    Kind kind;
};

// Record offsets, CCs and clamps from NOTES.md and scripts/engine_inputs.py.
const ParamDef kTrackParams[] = {
    {"machine", "Machine", 0x26, 0, 5, 70, Kind::Sound},
    {"pitch", "Pitch", 0x28, 40, 88, 65, Kind::Sound},
    {"decay", "Decay", 0x38, 0, 127, 80, Kind::Sound},
    {"color", "Color", 0x2a, 0, 127, 16, Kind::Sound},
    {"shape", "Shape", 0x2c, 0, 127, 17, Kind::Sound},
    {"sweep", "Sweep", 0x2e, 0, 127, 18, Kind::Sound},
    {"contour", "Contour", 0x30, 0, 127, 19, Kind::Sound},
    {"punch", "Punch", 0x32, 0, 1, 66, Kind::Sound},
    {"gate", "Gate", 0x34, 0, 1, 67, Kind::Sound},
    {"voldist", "Volume+Dist", 0x3a, 0, 127, 7, Kind::Sound},
    {"pan", "Pan", 0x40, 0, 127, 10, Kind::Sound},
    {"delaysend", "Delay Send", 0x3c, 0, 127, 12, Kind::Sound},
    {"reverbsend", "Reverb Send", 0x3e, 0, 127, 13, Kind::Sound},
    {"lfospeed", "LFO Speed", 0x16, 0, 127, 102, Kind::Sound},
    {"lfomult", "LFO Multiplier", 0x18, 0, 23, 103, Kind::Sound},
    {"lfofade", "LFO Fade", 0x1a, 0, 127, 104, Kind::Sound},
    {"lfowave", "LFO Waveform", 0x1e, 0, 6, 106, Kind::Sound},
    {"lfophase", "LFO Start Phase", 0x20, 0, 127, 107, Kind::Sound},
    {"lforeset", "LFO Reset", 0x22, 0, 4, 108, Kind::Sound},
    {"lfodepth", "LFO Depth", 0x24, 0, 127, 109, Kind::Sound},
    {"level", "Level", 0, 0, 127, 95, Kind::Level},
    {"mute", "Mute", 0, 0, 1, 94, Kind::Mute},
};

const ParamDef kFxParams[] = {
    {"delaytime", "Delay Time", 0x12, 0, 127, 85, Kind::Fx},
    {"delayfeedback", "Delay Feedback", 0x14, 0, 127, 86, Kind::Fx},
    {"reverbsize", "Reverb Size", 0x16, 0, 127, 87, Kind::Fx},
    {"reverbtone", "Reverb Tone", 0x18, 0, 127, 88, Kind::Fx},
};

const char* kMachines[] = {"Kick", "Snare", "Metal", "Perc", "Tone", "Chord"};

juce::File stateDir()
{
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Application Support/CyclesEmu/state");
}
} // namespace

class CyclesEmuProcessor : public juce::AudioProcessor
{
public:
    CyclesEmuProcessor()
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
        for (auto& s : slots)
        {
            s.param = apvts.getParameter(s.id);
            int v = engine ? currentEngineValue(s) : s.def->lo;
            s.lastParam = v;
            s.param->setValueNotifyingHost(s.param->convertTo0to1((float)v));
        }
    }

    ~CyclesEmuProcessor() override { mc_engine_free(engine); }

    // -- parameters ------------------------------------------------------
    struct Slot
    {
        juce::String id;
        const ParamDef* def;
        int track;   // 0-5, 6 for FX
        juce::RangedAudioParameter* param = nullptr;
        int lastParam = -1;
    };

    juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        for (int t = 0; t < kTracks; ++t)
        {
            auto group = std::make_unique<juce::AudioProcessorParameterGroup>(
                "t" + juce::String(t + 1), "Track " + juce::String(t + 1), "|");
            for (const auto& d : kTrackParams)
            {
                juce::String id = "t" + juce::String(t + 1) + "_" + d.id;
                juce::String name = "T" + juce::String(t + 1) + " " + d.name;
                if (std::strcmp(d.id, "machine") == 0)
                    group->addChild(std::make_unique<juce::AudioParameterChoice>(
                        juce::ParameterID{id, 1}, name, juce::StringArray(kMachines, 6), 0));
                else if (d.hi == 1)
                    group->addChild(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{id, 1}, name, false));
                else
                    group->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{id, 1}, name, d.lo, d.hi, d.lo));
                slots.push_back({id, &d, t});
            }
            layout.add(std::move(group));
        }
        layout.add(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID{"midimode", 1}, "MIDI Mode",
            juce::StringArray{"Drum Map (C1-F1 = tracks)", "Ch 1-6 = Tracks"}, 0));
        layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{"keystrack", 1}, "Keys Track", 1, 6, 1));
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"outgain", 1}, "Output Gain",
            juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel("dB")));
        auto fx = std::make_unique<juce::AudioProcessorParameterGroup>("fx", "FX", "|");
        for (const auto& d : kFxParams)
        {
            juce::String id = juce::String("fx_") + d.id;
            fx->addChild(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{id, 1}, d.name, d.lo, d.hi, d.lo));
            slots.push_back({id, &d, 6});
        }
        layout.add(std::move(fx));
        return layout;
    }

    int currentEngineValue(const Slot& s) const
    {
        switch (s.def->kind)
        {
            case Kind::Level: return mc_engine_get_level(engine, s.track);
            case Kind::Mute: return mc_engine_get_mute(engine, s.track);
            default: return juce::jlimit(s.def->lo, s.def->hi, mc_engine_get_param(engine, s.track, s.def->offset));
        }
    }

    void applyToEngine(const ParamDef& d, int track, int v)
    {
        v = juce::jlimit(d.lo, d.hi, v);
        switch (d.kind)
        {
            case Kind::Sound: mc_engine_param(engine, track, d.offset, v); break;
            case Kind::Fx: mc_engine_param(engine, 6, d.offset, v); break;
            case Kind::Level: mc_engine_level(engine, track, v); break;
            case Kind::Mute: mc_engine_mute(engine, track, v != 0); break;
        }
    }

    // Host parameter changes since the last block: applied only when the
    // parameter itself moved, so a CC that set the engine is not undone.
    void applyParameterChanges()
    {
        for (auto& s : slots)
        {
            int v = juce::roundToInt(s.param->convertFrom0to1(s.param->getValue()));
            if (v != s.lastParam)
            {
                s.lastParam = v;
                applyToEngine(*s.def, s.track, v);
            }
        }
    }

    // -- MIDI -------------------------------------------------------------
    void handleMidi(const juce::MidiMessage& m)
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
            if (m.isNoteOn())
                mc_engine_note(engine, track, note, m.getVelocity(), 1, kTrigFlags);
            else
                mc_engine_note(engine, track, note, 0, 0, kTrigFlags);
            return;
        }
        if (ch < 0 || ch >= kTracks)
            return;
        if (m.isController())
        {
            int cc = m.getControllerNumber(), v = m.getControllerValue();
            for (const auto& d : kTrackParams)
                if (d.cc == cc)
                {
                    // punch and gate: the firmware stored 1 for every non-zero value tried
                    applyToEngine(d, ch, d.hi == 1 ? (v != 0) : v);
                    return;
                }
            for (const auto& d : kFxParams)
                if (d.cc == cc)
                {
                    applyToEngine(d, 6, v);
                    return;
                }
        }
    }

    // -- audio ------------------------------------------------------------
    void prepareToPlay(double sampleRate, int) override
    {
        hostRate = sampleRate;
        resample = std::abs(sampleRate - kEngineRate) > 0.5;
        ratio = kEngineRate / sampleRate;
        for (auto& r : resamplers)
            r.reset();
        dcL.reset();
        dcR.reset();
        srcL.clear();
        srcR.clear();
        srcL.reserve(8192);
        srcR.reserve(8192);
        carry.ensureSize(4096);
        blockPos = kBlock;
        setLatencySamples(resample ? juce::roundToInt(juce::WindowedSincInterpolator::getBaseLatency() / ratio) : 0);
    }

    void releaseResources() override {}

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
    }

    void renderEngineBlock()
    {
        applyParameterChanges();
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

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override
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
            return;
        }

        // Resampled: this call's events go in at its start, then enough 48 kHz
        // engine output is produced to interpolate n host samples.
        for (const auto meta : midi)
            handleMidi(meta.getMessage());
        const int needed = (int)std::ceil(n * ratio) + 4;
        while ((int)srcL.size() < needed)
        {
            renderEngineBlock();
            srcL.insert(srcL.end(), blockL, blockL + kBlock);
            srcR.insert(srcR.end(), blockR, blockR + kBlock);
        }
        int usedL = resamplers[0].process(ratio, srcL.data(), outL, n);
        int usedR = resamplers[1].process(ratio, srcR.data(), outR, n);
        jassert(usedL == usedR);
        srcL.erase(srcL.begin(), srcL.begin() + usedL);
        srcR.erase(srcR.begin(), srcR.begin() + usedR);
    }

    // -- plumbing ---------------------------------------------------------
    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor(*this); }
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "CyclesEmu"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& dest) override
    {
        if (auto xml = apvts.copyState().createXml())
            copyXmlToBinary(*xml, dest);
    }

    void setStateInformation(const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary(data, size))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
    }

private:
    mc_engine* engine = nullptr;
    juce::String engineError;
    std::vector<Slot> slots;
    juce::AudioProcessorValueTreeState apvts;
    float blockL[kBlock] = {}, blockR[kBlock] = {};
    int blockPos = kBlock;
    juce::MidiBuffer carry;           // events waiting for the next block boundary
    double hostRate = kEngineRate, ratio = 1.0;
    bool resample = false;
    juce::WindowedSincInterpolator resamplers[2];
    std::vector<float> srcL, srcR;
    int engineFaults = 0;
    DcBlock dcL, dcR;
    std::atomic<float>* midiMode = nullptr;
    std::atomic<float>* keysTrack = nullptr;
    std::atomic<float>* outGain = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CyclesEmuProcessor)
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new CyclesEmuProcessor(); }
