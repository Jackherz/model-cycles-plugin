// Controlled randomisation of one section of one track. Each parameter has
// its own distribution, weighted toward values that tend to sound good
// (pitch on musical intervals, sends often off, LFO speeds and multipliers
// on musical values, the filter usually off or moderate). The machine,
// level and mute are left alone. A variation nudges the current values
// instead of drawing new ones.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace cyclesemu
{
enum class RandomSection { Sound, Lfo, Filter };

void randomise(juce::AudioProcessorValueTreeState&, int track, RandomSection, juce::Random&, bool variation);
} // namespace cyclesemu
