// Render every page of the CyclesEmu editor to PNG (layout check).
//   CyclesEmuEditorShot OUT_DIR
#include "../Source/PluginEditor.h"
#include <map>

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
    // the LFO display with every setting in play: triangle x4 at speed +32
    // (one cycle a bar), trig mode Trig, start phase 32 (a quarter), fade in
    auto set = [&](const char* id, float v) {
        auto* p = proc.state().getParameter(id);
        p->setValueNotifyingHost(p->convertTo0to1(v));
    };
    set("t1_lfowave", 0); set("t1_lforeset", 1); set("t1_lfosync", 0); set("t1_lfomult", 2);
    set("t1_lfospeed", 96); set("t1_lfodepth", 112); set("t1_lfofade", 40); set("t1_lfophase", 32);
    set("t1_lfodest", 4);
    e->showPage(0);
    {
        auto img = e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f);
        auto f = out.getChildFile("editor-lfo-demo.png");
        f.deleteFile();
        juce::FileOutputStream os(f);
        juce::PNGImageFormat().writeImageToStream(img, os);
    }
    // faster: more cycles in the bar (x16 at +40: 5 a bar), then the cap
    for (auto [mult, speed, name] : {std::tuple{4, 104, "editor-lfo-5cycles.png"},
                                     std::tuple{11, 127, "editor-lfo-fast.png"}})
    {
        set("t1_lfomult", (float)mult);
        set("t1_lfospeed", (float)speed);
        set("t1_lfofade", 64);
        auto img = e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f);
        auto f = out.getChildFile(name);
        f.deleteFile();
        juce::FileOutputStream os(f);
        juce::PNGImageFormat().writeImageToStream(img, os);
    }

    // the filter page: a resonant low-pass with some envelope, on track 1
    set("t1_fcut", 40); set("t1_freso", 96); set("t1_fenv", 84); set("t1_fdec", 50);
    e->showPage(0);
    {
        auto img = e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f);
        auto f = out.getChildFile("editor-filter.png");
        f.deleteFile();
        juce::FileOutputStream os(f);
        juce::PNGImageFormat().writeImageToStream(img, os);
    }

    // a Syntakt engine: track 2 on SY Bits, its knobs relabelled
    set("t2_machine", 9);
    e->showPage(1);
    {
        auto img = e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f);
        auto f = out.getChildFile("editor-sybits.png");
        f.deleteFile();
        juce::FileOutputStream os(f);
        juce::PNGImageFormat().writeImageToStream(img, os);
    }

    // presets: track round trip (randomised track 1 -> file -> track 4),
    // kit round trip, and what the randomiser draws
    {
        using namespace cyclesemu;
        auto& st = proc.state();
        auto plain = [&](const juce::String& id) {
            auto* p = st.getParameter(id);
            return juce::roundToInt(p->convertFrom0to1(p->getValue()));
        };
        juce::Random rng(1234);
        for (auto s : {RandomSection::Sound, RandomSection::Lfo, RandomSection::Filter})
            randomise(st, 0, s, rng, false);
        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cyclesemu-test.cytrack");
        writeTree(trackToTree(st, 0), tmp);
        const int level4 = plain(paramId(3, "level"));
        treeToTrack(st, 3, readTree(tmp));
        int diff = 0;
        for (int i = 0; i < kNumTrackParams; ++i)
        {
            juce::String id = kTrackParams[i].id;
            if (id != "level" && id != "mute" && plain(paramId(0, id.toRawUTF8())) != plain(paramId(3, id.toRawUTF8())))
                ++diff;
        }
        std::printf("track preset round trip: %d of %d parameters differ; level kept: %s\n", diff,
                    kNumTrackParams - 2, plain(paramId(3, "level")) == level4 ? "yes" : "NO");
        auto kitFile = tmp.withFileExtension("cykit");
        writeTree(kitToTree(st), kitFile);
        std::vector<int> before;
        for (auto* p : proc.getParameters())
            before.push_back(juce::roundToInt(p->getValue() * 1000));
        for (int t = 0; t < kTracks; ++t)
            randomise(st, t, RandomSection::Sound, rng, false);
        setPlain(st, "fx_delaytime", 3);
        treeToKit(st, readTree(kitFile));
        int kd = 0, k = 0;
        for (auto* p : proc.getParameters())
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p);
            const auto id = rp ? rp->getParameterID() : juce::String();
            const bool inKit = id.startsWith("t") || id.startsWith("fx_");
            if (inKit && juce::roundToInt(p->getValue() * 1000) != before[(size_t)k])
                ++kd;
            ++k;
        }
        std::printf("kit round trip: %d parameters differ\n", kd);
        tmp.deleteFile();
        kitFile.deleteFile();
        // the randomiser's draws over many tries
        std::map<int, int> cut, pitch, mult;
        for (int i = 0; i < 2000; ++i)
        {
            randomise(st, 5, RandomSection::Filter, rng, false);
            randomise(st, 5, RandomSection::Sound, rng, false);
            randomise(st, 5, RandomSection::Lfo, rng, false);
            int c = plain("t6_fcut");
            cut[c == 64 ? 0 : c < 64 ? 1 : 2]++;
            pitch[plain("t6_pitch") - 64]++;
            mult[plain("t6_lfomult")]++;
        }
        std::printf("filter: off %d, low-pass %d, high-pass %d of 2000\n", cut[0], cut[1], cut[2]);
        std::printf("pitch offsets:");
        for (auto [k2, n] : pitch) std::printf(" %+d:%d", k2, n);
        std::printf("\nlfo multipliers (index):");
        for (auto [k2, n] : mult) std::printf(" %d:%d", k2, n);
        std::printf("\n");
        randomise(st, 0, RandomSection::Sound, rng, true);   // a variation, for the screenshot
        e->showPage(0);
        auto img = e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f);
        auto f = out.getChildFile("editor-randomised.png");
        f.deleteFile();
        juce::FileOutputStream os(f);
        juce::PNGImageFormat().writeImageToStream(img, os);
    }

    // the help mode, as if hovering over a few controls
    const char* help[][3] = {{"0", "t1_color", "help-color"}, {"0", "t1_lforeset", "help-trigmode"}, {"1", "t2_color", "help-sybits-color"},
                             {"6", "fx_reverbsize", "help-reverbsize"}, {"0", "midimode", "help-midimode"}};
    for (auto& h : help)
    {
        e->showPage(juce::String(h[0]).getIntValue());
        e->showHelpFor(h[1]);
        auto img = e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f);
        auto f = out.getChildFile(juce::String("editor-") + h[2] + ".png");
        f.deleteFile();
        juce::FileOutputStream os(f);
        juce::PNGImageFormat().writeImageToStream(img, os);
        std::printf("%s\n", f.getFullPathName().toRawUTF8());
    }
    return 0;
}
