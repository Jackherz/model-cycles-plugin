#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

namespace cyclesemu
{
// The kit-saved fanfare: a short chiptune flourish (two pulse voices, a
// triangle bass and noise drums), our own tune, rendered at 44.1 kHz.
juce::AudioBuffer<float> renderFanfare();

// Plays it on the Mac's own output (afplay, from a WAV written once to the
// settings folder), not into the plugin's: it stays out of the DAW's mix and
// its bounces. Off with "fanfare" = 0 in the settings (View menu).
void playFanfare();
bool fanfareOn();
void setFanfareOn(bool);
} // namespace cyclesemu
