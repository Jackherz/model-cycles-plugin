// What each parameter is and does, for the editor's "?" help mode.
#pragma once

#include <juce_core/juce_core.h>

namespace cyclesemu
{
struct HelpText
{
    juce::String title, body;
};

// id: a parameter id ("t3_color", "fx_reverbsize", "tempo"); machine: the
// track's machine (0-5), for the macros whose meaning depends on it.
HelpText helpFor(const juce::String& id, int machine);
} // namespace cyclesemu
