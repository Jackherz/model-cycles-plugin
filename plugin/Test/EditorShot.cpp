// Render every page of the CyclesEmu editor to PNG (layout check).
//   CyclesEmuEditorShot OUT_DIR
#include "../Source/PluginEditor.h"

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::File out(argc > 1 ? argv[1] : ".");
    CyclesEmuProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    auto* e = dynamic_cast<CyclesEmuEditor*>(ed.get());
    const char* names[] = {"track1", "track2", "track3", "track4", "track5", "track6", "fx"};
    for (int i = 0; i < 7; ++i)
    {
        e->showPage(i);
        auto img = e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f);
        auto f = out.getChildFile(juce::String("editor-") + names[i] + ".png");
        f.deleteFile();
        juce::FileOutputStream os(f);
        juce::PNGImageFormat().writeImageToStream(img, os);
        std::printf("%s\n", f.getFullPathName().toRawUTF8());
    }
    return 0;
}
