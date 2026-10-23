// SP TWINSHOT's sample bank (NOTES.md step 38): up to 64 WAVs from a folder,
// in name order, mono 16-bit at 48 kHz, written into the engine where the
// Syntakt's console would have put its factory samples: the data in spare
// memory, and the engine's table of 64 {length, pointer} entries (the
// Syntakt's 0x4404f754, which the port's BSS window puts at 0x43030754).
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include "engine.h"

namespace cyclesemu
{
constexpr int kTwinSlots = 64;

class TwinSamples
{
public:
    // reads a folder (WAV and AIFF; hidden and "._" files skipped); returns
    // how many loaded. Message thread.
    int loadFolder(const juce::File& folder);
    // into an engine's memory (any thread that owns the engine between blocks)
    void install(mc_engine*) const;
    int count() const { return bank->names.size(); }
    juce::String name(int slot) const { return slot >= 0 && slot < count() ? bank->names[slot] : juce::String(); }
    // what a Sample knob's value plays: 64..127 slots 0..63 forward, 63..0
    // the same slots reversed (the engine's own reading)
    juce::String describe(int knob) const;
    juce::File folder;

private:
    struct Bank
    {
        juce::StringArray names;
        std::vector<std::vector<uint8_t>> data;   // big-endian 16-bit
    };
    // shared by every instance loading the same folder (unchanged files)
    std::shared_ptr<const Bank> bank = std::make_shared<Bank>();
    static std::shared_ptr<const Bank> read(const juce::File& folder);
};
} // namespace cyclesemu
