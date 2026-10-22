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
    const char* names[] = {"track1", "track2", "track3", "track4", "track5", "track6", "mix", "fx"};
    for (int i = 0; i < 8; ++i)
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
            if (id != "level" && id != "mute" && id != "solo"
                && plain(paramId(0, id.toRawUTF8())) != plain(paramId(3, id.toRawUTF8())))
                ++diff;
        }
        std::printf("track preset round trip: %d of %d parameters differ; level kept: %s\n", diff,
                    kNumTrackParams - 3, plain(paramId(3, "level")) == level4 ? "yes" : "NO");
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

    // resets: a randomised track 2 back to its machine's init sound (checked
    // against defaults.json), then a randomised kit back to the new kit
    {
        using namespace cyclesemu;
        auto& st = proc.state();
        juce::Random rng(99);
        auto plain = [&](const juce::String& id) {
            auto* p = st.getParameter(id);
            return juce::roundToInt(p->convertFrom0to1(p->getValue()));
        };
        const auto& fresh = proc.newKit();
        for (auto s : {RandomSection::Sound, RandomSection::Lfo, RandomSection::Filter})
            randomise(st, 1, s, rng, false);
        proc.resetTrack(1);
        auto json = juce::JSON::parse(juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
                                          .getChildFile("Application Support/CyclesEmu/state/defaults.json"));
        const int m = plain("t2_machine");
        int bad = 0, n = 0;
        for (int i = 0; i < kNumTrackParams; ++i)
            if (kTrackParams[i].kind == Kind::Sound)
            {
                const int want = json["sound"][juce::Identifier(juce::String(m))]
                                     [juce::Identifier(juce::String((kTrackParams[i].offset - 0x14) / 2))];
                bad += plain(paramId(1, kTrackParams[i].id)) != want;
                ++n;
            }
        std::printf("reset track 2 (machine %d): %d of %d sound parameters differ from the firmware's init sound\n",
                    m, bad, n);
        for (int t = 0; t < kTracks; ++t)
            randomise(st, t, RandomSection::Sound, rng, false);
        proc.resetKit();
        auto again = kitToTree(st);
        int kd = 0;
        for (int i = 0; i < fresh.getNumChildren(); ++i)
            kd += !fresh.getChild(i).isEquivalentTo(again.getChild(i));
        std::printf("reset kit: %d of %d kit sections differ from the new kit\n", kd, fresh.getNumChildren());
    }

    // typing a value: click track 1's Decay readout, type 100, Return
    {
        e->showPage(0);
        std::function<retro::PixelEntry*(juce::Component&, bool)> find =
            [&](juce::Component& c, bool inDecay) -> retro::PixelEntry* {
            inDecay = inDecay || c.getProperties()["helpId"].toString() == "t1_decay";
            if (auto* pe = dynamic_cast<retro::PixelEntry*>(&c); pe != nullptr && inDecay)
                return pe;
            for (auto* ch : c.getChildren())
                if (auto* f = find(*ch, inDecay))
                    return f;
            return nullptr;
        };
        retro::PixelEntry* entry = nullptr;
        for (auto* ch : e->getChildren())
            if (entry == nullptr)
                entry = find(*ch, false);
        if (entry != nullptr)
        {
            entry->begin();
            for (auto ch : juce::String("100"))
                entry->keyPressed(juce::KeyPress(ch, 0, ch));
            auto img = e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f);
            auto f = out.getChildFile("editor-typing.png");
            f.deleteFile();
            juce::FileOutputStream os(f);
            juce::PNGImageFormat().writeImageToStream(img, os);
            entry->keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
            auto* p = proc.state().getParameter("t1_decay");
            std::printf("typed 100 into Decay: the parameter is now %d\n",
                        juce::roundToInt(p->convertFrom0to1(p->getValue())));
        }
        else
            std::printf("no Decay readout found\n");
    }

    // the five colour schemes, on track 1 (applied directly: no settings file)
    e->showPage(0);
    for (int i = 0; i < retro::kNumThemes; ++i)
    {
        retro::applyTheme(i);
        auto img = e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f);
        auto f = out.getChildFile(juce::String("editor-colour-") + retro::kThemeNames[i] + ".png");
        f.deleteFile();
        juce::FileOutputStream os(f);
        juce::PNGImageFormat().writeImageToStream(img, os);
    }
    retro::applyTheme(0);

    // knob ticks: each track colour against each scheme's face, before and
    // after readableOn; and track 2 (yellow) on every scheme
    std::printf("tick contrast (raw -> drawn), tracks 1-6:\n");
    e->showPage(1);
    for (int i = 0; i < retro::kNumThemes; ++i)
    {
        retro::applyTheme(i);
        std::printf("  %-9s", retro::kThemeNames[i]);
        for (int t = 0; t < 6; ++t)
        {
            auto a = retro::kAccents[t];
            std::printf(" %4.1f->%4.1f", retro::contrast(a, retro::kFace),
                        retro::contrast(retro::readableOn(a, retro::kFace, 2.0f), retro::kFace));
        }
        std::printf("\n");
        auto img = e->createComponentSnapshot({20, 290, 560, 260}, true, 1.0f);
        auto f = out.getChildFile(juce::String("editor-knobs-") + retro::kThemeNames[i] + ".png");
        f.deleteFile();
        juce::FileOutputStream os(f);
        juce::PNGImageFormat().writeImageToStream(img, os);
    }
    retro::applyTheme(0);

    // musical delay times: names at note lengths, typed names parse, other
    // values plain numbers; the snap points cover them
    {
        auto* p = proc.state().getParameter("fx_delaytime");
        auto text = [&](int v) { return p->getText(p->convertTo0to1((float)v), 16); };
        const int typed = juce::roundToInt(p->convertFrom0to1(p->getValueForText("1/4.")));
        const bool ok = text(16) == "1/8" && text(24) == "1/8." && text(32) == "1/4" && text(30) == "30"
                        && typed == 48 && cyclesemu::snapPoints("delaytime").size() == 12
                        && cyclesemu::snapPoints("decay").empty();
        std::printf("delay time names: 16=%s 24=%s 32=%s 30=%s, \"1/4.\" types %d -> %s\n", text(16).toRawUTF8(),
                    text(24).toRawUTF8(), text(32).toRawUTF8(), text(30).toRawUTF8(), typed, ok ? "PASS" : "FAIL");
    }

    // hardware defaults: after Reset Track, every parameter sits at its
    // default (what double-click and the host's reset go to); and each
    // parameter reports that default to the host
    {
        using namespace cyclesemu;
        CyclesEmuProcessor f(Product::Full);
        juce::Random rng(3);
        setPlain(f.state(), "t3_machine", 7);
        for (auto s : {RandomSection::Sound, RandomSection::Lfo, RandomSection::Filter})
            randomise(f.state(), 2, s, rng, false);
        f.resetTrack(2);
        int bad = 0, hostBad = 0, n = 0;
        for (int i = 0; i < kNumTrackParams; ++i)
        {
            const auto id = paramId(2, kTrackParams[i].id);
            auto* p = f.state().getParameter(id);
            const float d = f.defaultFor(id), now = p->convertFrom0to1(p->getValue());
            bad += d < 0 || juce::roundToInt(d) != juce::roundToInt(now);
            hostBad += std::abs(p->getDefaultValue() - p->convertTo0to1(d)) > 1e-6f;
            ++n;
        }
        std::printf("hardware defaults: %d of %d parameters off their default after Reset Track; %d report "
                    "another to the host -> %s\n", bad, n, hostBad, bad == 0 && hostBad == 0 ? "PASS" : "FAIL");
    }

    // mute: a note on track 1 with Mute off, Mute on before the note, and Mute
    // switched on while it sounds
    {
        auto play = [&](int mute) {   // 0 off, 1 on before, 2 on after the note
            CyclesEmuProcessor f(Product::Full);
            cyclesemu::setPlain(f.state(), "midimode", 1);
            cyclesemu::setPlain(f.state(), "t1_machine", 4);
            cyclesemu::setPlain(f.state(), "t1_decay", 110);
            cyclesemu::setPlain(f.state(), "t1_mute", mute == 1 ? 1.0f : 0.0f);
            f.prepareToPlay(48000, 128);
            juce::AudioBuffer<float> buf(2, 128);
            double e = 0;
            for (int b = 0; b < 375 + 300; ++b)
            {
                juce::MidiBuffer midi;
                if (b == 375)
                    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)110), 0);
                if (mute >= 2 && b == 400)
                    cyclesemu::setPlain(f.state(), "t1_mute", 1.0f);
                if (mute == 3 && b == 410)   // and back
                    cyclesemu::setPlain(f.state(), "t1_mute", 0.0f);
                buf.clear();
                f.processBlock(buf, midi);
                if (b >= 420)
                    for (int i = 0; i < 128; ++i)
                        e += (double)buf.getSample(0, i) * buf.getSample(0, i);
            }
            return std::sqrt(e / (255 * 128.0));
        };
        const double off = play(0), before = play(1), during = play(2), back = play(3);
        // solo: track 2 soloed silences track 1; track 1 soloed too plays it
        auto soloed = [&](bool alsoOne) {
            CyclesEmuProcessor f(Product::Full);
            cyclesemu::setPlain(f.state(), "midimode", 1);
            cyclesemu::setPlain(f.state(), "t1_machine", 4);
            cyclesemu::setPlain(f.state(), "t1_decay", 110);
            cyclesemu::setPlain(f.state(), "t2_solo", 1);
            cyclesemu::setPlain(f.state(), "t1_solo", alsoOne ? 1.0f : 0.0f);
            f.prepareToPlay(48000, 128);
            juce::AudioBuffer<float> buf(2, 128);
            double e = 0;
            for (int b = 0; b < 675; ++b)
            {
                juce::MidiBuffer midi;
                if (b == 375)
                    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)110), 0);
                buf.clear();
                f.processBlock(buf, midi);
                if (b >= 420)
                    for (int i = 0; i < 128; ++i)
                        e += (double)buf.getSample(0, i) * buf.getSample(0, i);
            }
            return std::sqrt(e / (255 * 128.0));
        };
        // the mixer's meters: a note on track 3 shows on track 3 only
        {
            CyclesEmuProcessor f(Product::Full);
            cyclesemu::setPlain(f.state(), "midimode", 1);
            f.prepareToPlay(48000, 128);
            juce::AudioBuffer<float> buf(2, 128);
            for (int b = 0; b < 400; ++b)
            {
                juce::MidiBuffer midi;
                if (b == 375)
                    midi.addEvent(juce::MidiMessage::noteOn(3, 60, (juce::uint8)110), 0);
                if (b == 374)
                    for (int t = 0; t < 6; ++t)
                        f.takeTrackPeak(t);   // forget the start
                buf.clear();
                f.processBlock(buf, midi);
            }
            float pk[6];
            bool ok = true;
            for (int t = 0; t < 6; ++t)
            {
                pk[t] = f.takeTrackPeak(t);
                ok &= t == 2 ? pk[t] > 0.01f : pk[t] < 1e-6f;
            }
            std::printf("meters after a note on track 3: %.3f %.3f %.3f %.3f %.3f %.3f -> %s\n", pk[0], pk[1], pk[2],
                        pk[3], pk[4], pk[5], ok ? "PASS" : "FAIL");
        }
        const double other = soloed(false), both = soloed(true);
        std::printf("solo: track 1 with track 2 soloed %.4f, with both soloed %.4f -> %s\n", other, both,
                    other < off * 0.001 && std::abs(both / off - 1.0) < 0.05 ? "PASS" : "FAIL");
        std::printf("mute: off %.4f, on before the note %.4f, on while it sounds %.4f, on and off again %.4f -> %s\n",
                    off, before, during, back,
                    before < off * 0.001 && during < off * 0.01 && std::abs(back / off - 1.0) < 0.05 ? "PASS" : "FAIL");
    }

    // CyclesVoice in steady state: settings first, a second of silence, then
    // the notes; the level over the 0.8 s after them. One note, the same note
    // on three voices, a chord. (Notes right after a machine change catch the
    // firmware still settling it: a transient, not a voice difference.)
    {
        auto steady = [&](std::vector<int> notes, int voices) {
            CyclesEmuProcessor v(Product::Voice);
            cyclesemu::setPlain(v.state(), "t1_machine", 4);
            cyclesemu::setPlain(v.state(), "t1_decay", 110);
            cyclesemu::setPlain(v.state(), "voices", (float)voices);
            v.prepareToPlay(48000, 128);
            juce::AudioBuffer<float> buf(2, 128);
            double e = 0;
            const int at = 375, len = 300;   // 1 s, then 0.8 s
            for (int b = 0; b < at + len; ++b)
            {
                juce::MidiBuffer midi;
                if (b == at)
                    for (int k : notes)
                        midi.addEvent(juce::MidiMessage::noteOn(1, k, (juce::uint8)110), 0);
                buf.clear();
                v.processBlock(buf, midi);
                if (b >= at)
                    for (int i = 0; i < 128; ++i)
                        e += (double)buf.getSample(0, i) * buf.getSample(0, i);
            }
            return std::sqrt(e / (len * 128.0));
        };
        const double one = steady({60}, 6), three = steady({60, 60, 60}, 6), chord = steady({60, 64, 67}, 6),
                     n64 = steady({64}, 6), n67 = steady({67}, 6), mono = steady({60, 64, 67}, 1);
        const double sum = std::sqrt(one * one + n64 * n64 + n67 * n67);
        const bool ok = std::abs(three / one - 3.0) < 0.05 && std::abs(chord / sum - 1.0) < 0.05
                        && std::abs(mono / one - 1.0) < 0.3;
        std::printf("voice, steady: one %.4f, same note x3 %.4f (x%.2f), chord %.4f (single notes' energies: %.4f), "
                    "chord at 1 voice %.4f -> %s\n",
                    one, three, three / one, chord, sum, mono, ok ? "PASS" : "FAIL");
    }

    // CyclesFX's editor
    {
        CyclesEmuProcessor fx(Product::Fx);
        std::unique_ptr<juce::AudioProcessorEditor> fe(fx.createEditor());
        auto img = fe->createComponentSnapshot(fe->getLocalBounds(), true, 1.0f);
        auto f = out.getChildFile("fx-plugin.png");
        f.deleteFile();
        juce::FileOutputStream os(f);
        juce::PNGImageFormat().writeImageToStream(img, os);
    }

    // CyclesVoice's editor: the voice page and its FX page
    {
        CyclesEmuProcessor voice(Product::Voice);
        // every voice must hold the one sound: compare tracks 2-6 with track 1
        using namespace cyclesemu;
        int differ = 0;
        for (int i = 0; i < kNumTrackParams; ++i)
        {
            const auto& d = kTrackParams[i];
            const int off = d.kind == Kind::Level ? -1 : d.kind == Kind::Mute ? -2 : d.offset;
            for (int t = 1; t < kTracks; ++t)
                if (voice.engineValue(t, off) != voice.engineValue(0, off))
                {
                    if (differ++ < 12)
                        std::printf("  voice %d differs on %s: %d, voice 1 %d\n", t + 1, d.id, voice.engineValue(t, off),
                                    voice.engineValue(0, off));
                }
        }
        std::printf("voice: %d engine values differ between the voices at start -> %s\n", differ,
                    differ == 0 ? "PASS" : "FAIL");
        std::unique_ptr<juce::AudioProcessorEditor> ve(voice.createEditor());
        auto* v = dynamic_cast<CyclesEmuEditor*>(ve.get());
        for (int i = 0; i < 2; ++i)
        {
            v->showPage(i);
            auto img = v->createComponentSnapshot(v->getLocalBounds(), true, 1.0f);
            auto f = out.getChildFile(i == 0 ? "voice-page.png" : "voice-fx.png");
            f.deleteFile();
            juce::FileOutputStream os(f);
            juce::PNGImageFormat().writeImageToStream(img, os);
        }
    }

    // the help mode, as if hovering over a few controls
    const char* help[][3] = {{"0", "t1_color", "help-color"}, {"0", "t1_lforeset", "help-trigmode"}, {"1", "t2_color", "help-sybits-color"},
                             {"7", "fx_reverbsize", "help-reverbsize"}, {"0", "midimode", "help-midimode"}};
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
