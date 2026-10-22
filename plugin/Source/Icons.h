// 9x9 pixel pictograms, after Fors' parameter symbols: one per machine,
// LFO waveform, LFO trig mode and LFO clock (sync/free).
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace cyclesemu
{
enum class IconSet { Machine, Wave, Trig, Clock, Dice };

// Centred in area, `scale` screen pixels per icon pixel.
void drawIcon(juce::Graphics&, IconSet, int index, juce::Rectangle<int> area, juce::Colour, int scale = 2);
} // namespace cyclesemu
