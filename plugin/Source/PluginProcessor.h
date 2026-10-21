// CyclesEmu: the Model:Cycles engine (the firmware's own render, compiled
// ahead of time, ../aot) as an instrument. See PluginProcessor.cpp.
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "engine.h"
#include "DcBlock.h"

namespace cyclesemu
{
constexpr int kTracks = 6;
constexpr int kBlock = 32;

// How a host parameter reaches the engine.
enum class Kind
{
    Sound,      // a word of the track's sound record (offset)
    Fx,         // a word of the kit's FX block (offset)
    Level,      // track level (kit header)
    Mute,
    LfoMult,    // with LfoSync: the firmware's multiplier 0-23 (x1..x2k synced, 1..2k free)
    LfoSync,
    LfoDest,    // index into kLfoDestValues
};

// How the editor and the host show its value.
enum class Show { Plain, Bipolar, Semitones, Pan };

struct ParamDef
{
    const char* id;
    const char* name;
    int offset;       // record offset (sound) or FX offset
    int lo, hi;       // the host parameter's range
    int cc;           // the CC that sets it on the hardware, -1 if none
    Kind kind;
    Show show;
};

extern const ParamDef kTrackParams[];
extern const int kNumTrackParams;
extern const ParamDef kFxParams[];
extern const int kNumFxParams;

extern const juce::StringArray kMachineNames, kLfoWaveNames, kLfoTrigNames, kLfoMultNames, kLfoDestNames;
extern const int kLfoDestValues[14];

juce::String paramId(int track, const char* id);   // "t1_decay", "fx_delaytime", "midimode"
} // namespace cyclesemu

class CyclesEmuProcessor : public juce::AudioProcessor
{
public:
    CyclesEmuProcessor();
    ~CyclesEmuProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
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
    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int size) override;

    juce::AudioProcessorValueTreeState& state() { return apvts; }
    const juce::String& engineStatus() const { return engineError; }
    double currentBpm() const { return shownBpm.load(); }

private:
    struct Slot
    {
        juce::String id;
        const cyclesemu::ParamDef* def;
        int track;   // 0-5, 6 for FX
        juce::RangedAudioParameter* param = nullptr;
        int lastParam = -1;
    };

    juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    int currentEngineValue(const Slot&) const;
    void applyToEngine(const cyclesemu::ParamDef&, int track, int v);
    void applyLfoMult(int track);
    void applyParameterChanges();
    void handleMidi(const juce::MidiMessage&);
    void renderEngineBlock();
    int paramValue(int track, const char* id) const;

    mc_engine* engine = nullptr;
    juce::String engineError;
    std::vector<Slot> slots;
    juce::AudioProcessorValueTreeState apvts;
    float blockL[cyclesemu::kBlock] = {}, blockR[cyclesemu::kBlock] = {};
    int blockPos = cyclesemu::kBlock;
    juce::MidiBuffer carry;           // events waiting for the next block boundary
    double ratio = 1.0;
    bool resample = false;
    juce::WindowedSincInterpolator resamplers[2];
    std::vector<float> srcL, srcR;
    int engineFaults = 0;
    DcBlock dcL, dcR;
    std::atomic<float>* midiMode = nullptr;
    std::atomic<float>* keysTrack = nullptr;
    std::atomic<float>* outGain = nullptr;
    std::atomic<float>* tempoParam = nullptr;
    std::atomic<float>* followHost = nullptr;
    double targetBpm = 120.0, appliedBpm = 0.0;
    std::atomic<double> shownBpm{120.0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CyclesEmuProcessor)
};
