// CyclesEmu: the Model:Cycles engine (the firmware's own render, compiled
// ahead of time, ../aot) as an instrument. See PluginProcessor.cpp.
#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "engine.h"
#include "DcBlock.h"
#include "Resampler.h"

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
    Solo,       // the mixer's solo (the plugin's, not the firmware's)
    Glide,      // legato and portamento: the plugin's, read at each note
    Filter,     // the filter's type and third knob (mc_engine_set_filter)
};

// How the editor and the host show its value.
enum class Show { Plain, Bipolar, Semitones, Pan, Cutoff, NoteLength, GlideTime };   // NoteLength: 32 = a beat

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

extern const juce::StringArray kMachineNames, kLfoWaveNames, kLfoTrigNames, kLfoMultNames, kLfoDestNames,
    kFilterTypeNames;
// What a filter type's knobs are (cutoff, reso, third knob; empty: unused),
// and the settings a type starts from when picked (cutoff, reso, third):
// the Digitone II's defaults, so a new type starts open, flat or dry.
struct FilterTypeInfo
{
    const char *cut, *res, *extra;
    int defCut, defRes, defExtra;
};
extern const FilterTypeInfo kFilterTypes[7];
// The menu's order (the combs together) to the engine's type numbers
// (mc_engine_set_filter: the DN2's own order).
inline int filterEngineType(int menu)
{
    static const int map[7] = {0, 1, 2, 3, 5, 4, 6};
    return map[menu < 0 ? 0 : menu > 6 ? 6 : menu];
}
constexpr int kNumLfoDests = 18;
extern const int kLfoDestValues[kNumLfoDests];

juce::String paramId(int track, const char* id);   // "t1_decay", "fx_delaytime", "midimode"

// Musical values a knob snaps to while turned (bare parameter id), empty for
// none: the delay time's note lengths, the LFO speed's even rates.
const std::vector<int>& snapPoints(const juce::String& bareId);

// The portamento time (0-127) in milliseconds: 1 ms to 10 s, exponential.
double glideMs(int v);
} // namespace cyclesemu

// Which plugin a build is: CyclesEmu (six tracks, as the hardware),
// CyclesVoice (one sound played polyphonically on the six tracks as six
// voices) or CyclesFX (the delay and reverb as an audio effect, the host's
// audio fed into their sends). Each plugin target compiles CYCLES_PRODUCT.
enum class Product { Full, Voice, Fx };
#ifndef CYCLES_PRODUCT
#define CYCLES_PRODUCT 0
#endif

class CyclesEmuProcessor : public juce::AudioProcessor
{
public:
    explicit CyclesEmuProcessor(Product = (Product)CYCLES_PRODUCT);
    const Product product;
    bool isVoice() const { return product == Product::Voice; }
    bool isFx() const { return product == Product::Fx; }
    int trackPages() const { return isFx() ? 0 : isVoice() ? 1 : cyclesemu::kTracks; }
    ~CyclesEmuProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override
    {
        return isFx() ? "CyclesFX" : isVoice() ? "CyclesVoice" : "CyclesEmu";
    }
    bool acceptsMidi() const override { return !isFx(); }
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
    // Resets, as on the hardware: a track to its machine's init sound (the
    // firmware's own defaults, defaults.json in the state folder, from
    // scripts/init_sounds.py), the kit to the one a new project starts with
    // (the engine state's kit, as loaded). Message thread.
    bool canResetTrack() const { return haveInitSounds; }
    // A parameter's default as on the hardware (plain value): a track's
    // sound settings from its current machine's init sound, its level from
    // the new kit, mute and solo off; the FX from the new kit; anything else
    // its declared default. Knobs reset to it on double-click, and the
    // parameters report it to the host.
    float defaultFor(const juce::String& id) const;
    void resetTrack(int track);
    void resetKit();
    const juce::ValueTree& newKit() const { return initKit; }
    // for tests: what the engine holds (a sound word, or -1 level, -2 mute)
    int peek(uint32_t addr) const { return mc_engine_peek(engine, addr); }
    void setSmoothFilter(bool on) { mc_engine_set_smooth_filter(engine, on); }
    void pokeWord(int track, int offset, int word) { mc_engine_param_word(engine, track, offset, word); }
    uint32_t kitAddress() const { return mc_engine_kit(engine); }
    int engineValue(int track, int offset) const
    {
        return offset == -1 ? mc_engine_get_level(engine, track)
               : offset == -2 ? mc_engine_get_mute(engine, track) : mc_engine_get_param(engine, track, offset);
    }
    // the output's peak over the last callback (0..), for the editor's meter
    float outputPeak() const { return outPeak.load(std::memory_order_relaxed); }
    // a track's peak since the last call (0..1), after its level (post-fader): for the mixer's meters
    float takeTrackPeak(int t) { return trackPeaks[t].exchange(0.0f, std::memory_order_relaxed); }
    bool trackAudible(int t) const { return levelSent[t] != 0; }
    // note-ons per track (CyclesVoice: all on 0), for the editor's tab lights
    uint32_t trigCount(int t) const { return trigCounts[t].load(std::memory_order_relaxed); }

private:
    static BusesProperties busesFor(Product);
    struct Slot
    {
        juce::String id;
        const cyclesemu::ParamDef* def;
        int track;   // 0-5, 6 for FX
        juce::RangedAudioParameter* param = nullptr;
        std::atomic<float>* raw = nullptr;   // its plain value, read every block
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
    // CyclesVoice: the tracks as voices; the oldest free (or, all busy, the
    // oldest playing) voice takes the next note
    struct Voice
    {
        int note = -1;
        bool on = false;
        uint32_t age = 0;
    } voices[cyclesemu::kTracks];
    uint32_t voiceClock = 0;
    std::atomic<float>* voiceCount = nullptr;
    // CyclesFX: the input, 32 frames at 48 kHz per engine block, run through
    // the sends (mc_engine_set_input); the dry signal waits as long, for Mix
    std::atomic<float>*fxDelaySend = nullptr, *fxReverbSend = nullptr, *fxMix = nullptr;
    float inCollectL[cyclesemu::kBlock] = {}, inCollectR[cyclesemu::kBlock] = {};
    float inBlockL[cyclesemu::kBlock] = {}, inBlockR[cyclesemu::kBlock] = {};
    std::vector<float> inFifoL, inFifoR;   // host-rate input, when resampling
    Resampler inResampler;                 // host rate -> 48 kHz
    void fxProcess(juce::AudioBuffer<float>&);
    void fxEngineBlock(const float* inL, const float* inR, float* outL, float* outR);
    void voiceNote(int note, int velocity, bool on);
    // Legato and portamento, after the Syntakt's (NOTES.md step 28): the
    // engine plays the note it was triggered with, and the glide moves the
    // track's pitch word from there, in 1/256 semitones, every engine block.
    struct GlideSettings
    {
        bool port, legato, constTime, glissando, legatoOnly, gating;
        int priority;   // 0 last, 1 low, 2 high
        double ms, amount;
    };
    struct Glide
    {
        int trig = -1;           // the note the engine was last triggered with, -1 none yet
        int velocity = 100;
        double cur = 0, target = 0, step = 0;   // the pitch now and where it goes (notes), per block
        bool frozen = false, glissando = false;
        int held[16] = {}, numHeld = 0;   // keys down, oldest first (legato)
        int userPitch = 64, sentWord = 64 << 8;
    } glides[cyclesemu::kTracks];
    static constexpr int kGlideParams = 9;
    std::atomic<float>* glideRaw[cyclesemu::kTracks][kGlideParams] = {};
    int lastVoice = -1;   // CyclesVoice: the voice that played last, where a glide starts
    GlideSettings glideSettings(int page) const;
    void trackNote(int track, int note, int velocity, bool on);
    void startNote(int track, int note, int velocity, bool glide, double from, const GlideSettings&);
    void legatoTo(int track, int note, const GlideSettings&);
    void beginGlide(Glide&, double from, const GlideSettings&);
    void writePitch(int track, bool force = false);
    void updateGlides();
    void applyAll(const cyclesemu::ParamDef&, int track, int v);   // to every track in Voice
    // Mute and Solo as on a mixer: the firmware's mute only stops sequencer
    // trigs, so a track that should be silent (muted, or soloed out) has its
    // engine level held at 0, and its level is restored when it should play
    // again (audio thread only)
    int levelNow[cyclesemu::kTracks] = {100, 100, 100, 100, 100, 100};
    bool mutedNow[cyclesemu::kTracks] = {}, soloNow[cyclesemu::kTracks] = {};
    int levelSent[cyclesemu::kTracks] = {-1, -1, -1, -1, -1, -1};
    // the level as the firmware applies it: it eases each change, 3 % of
    // the way a block (measured, NOTES.md step 33), for the meters
    float levelEased[cyclesemu::kTracks] = {};
    int filterType[cyclesemu::kTracks] = {}, filterExtra[cyclesemu::kTracks] = {};
    void applyMix();
    // per-track peaks since the editor last looked, from the render's track
    // buffers (before level and pan)
    std::atomic<float> trackPeaks[cyclesemu::kTracks] = {};
    std::atomic<uint32_t> trigCounts[cyclesemu::kTracks] = {};
    void countTrig(int page) { trigCounts[page].fetch_add(1, std::memory_order_relaxed); }
    juce::String engineError;
    std::vector<Slot> slots;
    juce::AudioProcessorValueTreeState apvts;
    float blockL[cyclesemu::kBlock] = {}, blockR[cyclesemu::kBlock] = {};
    int blockPos = cyclesemu::kBlock;
    juce::MidiBuffer carry;           // events waiting for the next block boundary
    double ratio = 1.0;
    bool resample = false;
    Resampler resampler;
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
    std::atomic<float> outPeak{0.0f};
    int initSound[11][33] = {};
    bool haveInitSounds = false;
    juce::ValueTree initKit;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CyclesEmuProcessor)
};
