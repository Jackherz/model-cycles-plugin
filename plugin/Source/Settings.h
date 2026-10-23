// CyclesEmu's preferences, shared by every instance and product:
// ~/Library/Application Support/CyclesEmu/CyclesEmu.settings (the colour
// scheme, the Twinshot samples folder).
#pragma once
#include <juce_data_structures/juce_data_structures.h>

namespace cyclesemu
{
inline juce::PropertiesFile& settings()
{
    static juce::PropertiesFile file([] {
        juce::PropertiesFile::Options o;
        o.applicationName = "CyclesEmu";
        o.filenameSuffix = "settings";
        o.folderName = "CyclesEmu";
        o.osxLibrarySubFolder = "Application Support";
        return o;
    }());
    return file;
}
} // namespace cyclesemu
