// Track presets (one track's sound, LFO and filter) and kits (all six
// tracks with their levels and mutes, and the FX), as small XML files in
// ~/Documents/CyclesEmu/Tracks and ~/Documents/CyclesEmu/Kits. Values are
// the parameters' plain values by bare id ("decay", "lfospeed"), so a file
// still loads when parameters are added.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace cyclesemu
{
juce::File presetFolder(bool kits);   // created when missing

juce::ValueTree trackToTree(juce::AudioProcessorValueTreeState&, int track);
void treeToTrack(juce::AudioProcessorValueTreeState&, int track, const juce::ValueTree&);

juce::ValueTree kitToTree(juce::AudioProcessorValueTreeState&);
void treeToKit(juce::AudioProcessorValueTreeState&, const juce::ValueTree&);

bool writeTree(const juce::ValueTree&, const juce::File&);
juce::ValueTree readTree(const juce::File&);   // invalid if unreadable

// Set a parameter by plain value, as one host-visible gesture.
void setPlain(juce::AudioProcessorValueTreeState&, const juce::String& id, float value);
} // namespace cyclesemu
