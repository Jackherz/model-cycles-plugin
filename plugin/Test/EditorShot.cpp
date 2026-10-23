#include <cstring>
#include <complex>
#include <juce_dsp/juce_dsp.h>
#include <set>
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

    // the five colour schemes, on track 1 (applied directly: no settings
    // file), Punch and Gate ticked so the ticks show on each
    e->showPage(0);
    const float punch0 = proc.state().getParameter("t1_punch")->getValue(), gate0 = proc.state().getParameter("t1_gate")->getValue();
    cyclesemu::setPlain(proc.state(), "t1_punch", 1);
    cyclesemu::setPlain(proc.state(), "t1_gate", 1);
    for (int i = 0; i < retro::kNumThemes; ++i)
    {
        retro::applyTheme(i);
        auto img = e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f);
        auto f = out.getChildFile(juce::String("editor-colour-") + retro::kThemeNames[i] + ".png");
        f.deleteFile();
        juce::FileOutputStream os(f);
        juce::PNGImageFormat().writeImageToStream(img, os);
    }
    proc.state().getParameter("t1_punch")->setValueNotifyingHost(punch0);
    proc.state().getParameter("t1_gate")->setValueNotifyingHost(gate0);
    // the checkbox tick against its box, in every scheme
    {
        bool ok = true;
        std::printf("checkbox tick contrast:");
        for (int i = 0; i < retro::kNumThemes; ++i)
        {
            retro::applyTheme(i);
            const float c = retro::contrast(retro::readableOn(retro::kDark, retro::kField, 7.0f), retro::kField);
            std::printf(" %s %.1f", retro::kThemeNames[i], c);
            ok &= c >= 4.5f;
        }
        std::printf(" -> %s\n", ok ? "PASS" : "FAIL");
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
        // post-fader: a held Tone on track 1, its meter at levels 127, 64, 0
        {
            CyclesEmuProcessor f(Product::Full);
            cyclesemu::setPlain(f.state(), "midimode", 1);
            cyclesemu::setPlain(f.state(), "t1_machine", 4);
            cyclesemu::setPlain(f.state(), "t1_decay", 127);
            cyclesemu::setPlain(f.state(), "t1_lfodepth", 64);
            f.prepareToPlay(48000, 32);
            juce::AudioBuffer<float> buf(2, 32);
            float at[3] = {};
            const int levels[3] = {127, 64, 0};
            for (int b = 0; b < 1500 + 3 * 300; ++b)
            {
                juce::MidiBuffer midi;
                if (b == 1500)
                    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)110), 0);
                const int stage = b < 1500 ? -1 : (b - 1500) / 300;
                if (stage >= 0 && (b - 1500) % 300 == 0)
                    cyclesemu::setPlain(f.state(), "t1_level", (float)levels[stage]);
                buf.clear();
                f.processBlock(buf, midi);
                const float p = f.takeTrackPeak(0);
                if (stage >= 0 && (b - 1500) % 300 > 100)
                    at[stage] = std::max(at[stage], p);
            }
            const bool ok = at[0] > 0.05f && std::abs(at[1] / at[0] - 0.254f) < 0.02f && at[2] < 0.005f;   // level 0: faded, as the sound
            std::printf("meter post-fader: level 127 %.3f, 64 %.3f (x%.3f, (64/127)^2 = 0.254), 0 %.3f -> %s\n", at[0],
                        at[1], at[1] / at[0], at[2], ok ? "PASS" : "FAIL");
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

    // does a pitch word written during a note move it, finely and at once?
    // (the glide's way) Tone, a long note on track 1: frequency by zero
    // crossings before and after the word changes mid-note
    {
        auto freqs = [&](int word, double& before, double& after, double& justAfter) {
            CyclesEmuProcessor f(Product::Full);
            cyclesemu::setPlain(f.state(), "midimode", 1);
            cyclesemu::setPlain(f.state(), "t1_machine", 4);
            for (auto* id : {"t1_decay", "t1_color", "t1_shape", "t1_sweep", "t1_contour", "t1_lfodepth"})
                cyclesemu::setPlain(f.state(), id, juce::String(id) == "t1_decay" ? 127.0f
                                                   : juce::String(id) == "t1_lfodepth" ? 64.0f : 0.0f);
            f.prepareToPlay(48000, 32);
            juce::AudioBuffer<float> buf(2, 32);
            int c[3] = {};
            float prev = 0;
            for (int b = 0; b < 3200; ++b)
            {
                juce::MidiBuffer midi;
                if (b == 1500)
                    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)110), 0);
                if (b == 2300)
                    f.pokeWord(0, 0x28, word);
                buf.clear();
                f.processBlock(buf, midi);
                const int w = b >= 1600 && b < 2300 ? 0 : b >= 2300 && b < 2400 ? 1 : b >= 2500 ? 2 : -1;
                for (int i = 0; i < 32; ++i)
                {
                    const float s = buf.getSample(0, i);
                    if (w >= 0)
                        c[w] += prev < 0 && s >= 0;
                    prev = s;
                }
            }
            before = c[0] / ((700 * 32) / 48000.0);
            justAfter = c[1] / ((100 * 32) / 48000.0);
            after = c[2] / ((700 * 32) / 48000.0);
        };
        for (int word : {0x4080, 0x4400, 0x3c00})
        {
            double b, a, j;
            freqs(word, b, a, j);
            std::printf("pitch word 0x%04x mid-note: %.2f Hz -> %.2f Hz (%.3f st; first 67 ms %.3f st)\n", word, b, a,
                        12 * std::log2(a / b), 12 * std::log2(j / b));
        }
    }

    // the LFO reaches each destination: the engine's value is the list's, and
    // the smoothed word that moves is that parameter's (k = value), not pan's
    int lfoBad = 0;
    for (int destIndex : {13, 14, 15, 16, 17})   // Pan, Filt Cutoff, Reso, Env, Decay
    {
        CyclesEmuProcessor f(Product::Full);
        cyclesemu::setPlain(f.state(), "t1_lfodest", (float)destIndex);
        cyclesemu::setPlain(f.state(), "t1_lfodepth", 127);
        cyclesemu::setPlain(f.state(), "t1_lfospeed", 120);
        cyclesemu::setPlain(f.state(), "t1_lfosync", 1);
        cyclesemu::setPlain(f.state(), "t1_lfomult", 6);
        cyclesemu::setPlain(f.state(), "t1_lfowave", 0);    // triangle
        cyclesemu::setPlain(f.state(), "t1_lforeset", 0);   // free
        cyclesemu::setPlain(f.state(), "midimode", 1);
        f.prepareToPlay(48000, 32);
        juce::AudioBuffer<float> buf(2, 32);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)110), 0);
        int lo[33], hi[33];
        std::fill(lo, lo + 33, 1 << 20);
        std::fill(hi, hi + 33, -(1 << 20));
        for (int b = 0; b < 3000; ++b)
        {
            buf.clear();
            f.processBlock(buf, midi);
            midi.clear();
            if (b > 100)
                for (int k = 0; k < 33; ++k)
                {
                    const uint32_t a = 0x8000100cu + 14u + 2u * (uint32_t)k;
                    const int w = (int16_t)((f.peek(a) << 8) | f.peek(a + 1));
                    lo[k] = std::min(lo[k], w), hi[k] = std::max(hi[k], w);
                }
        }
        const int want = cyclesemu::kLfoDestValues[destIndex];
        const bool moves = hi[want] != lo[want], panMoves = want != 22 && hi[22] != lo[22];
        lfoBad += f.engineValue(0, 0x1c) != want || !moves || panMoves;
    }
    std::printf("lfo destinations Pan and the four filter ones each move their own word, not pan's -> %s\n",
                lfoBad == 0 ? "PASS" : "FAIL");

    // the LFO destination list follows the machine: track 1 on SD Vintage
    // names Shape "Freq Complex", and on Kick again "Shape"
    {
        e->showPage(0);
        std::function<ParamControl*(juce::Component&)> find = [&](juce::Component& c) -> ParamControl* {
            if (c.getProperties()["helpId"].toString() == "t1_lfodest")
                if (auto* pc = dynamic_cast<ParamControl*>(&c))
                    return pc;
            for (auto* ch : c.getChildren())
                if (auto* f = find(*ch))
                    return f;
            return nullptr;
        };
        ParamControl* dest = nullptr;
        for (auto* ch : e->getChildren())
            if (dest == nullptr)
                dest = find(*ch);
        const juce::String machine0 = juce::String(juce::roundToInt(proc.state().getRawParameterValue("t1_machine")->load()));
        cyclesemu::setPlain(proc.state(), "t1_machine", 6);
        cyclesemu::setPlain(proc.state(), "t1_lfodest", 3);
        const auto sdv = dest ? dest->itemText(3) : juce::String("?");
        auto img = e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f);
        auto f = out.getChildFile("editor-lfo-dest-sdvintage.png");
        f.deleteFile();
        juce::FileOutputStream os(f);
        juce::PNGImageFormat().writeImageToStream(img, os);
        cyclesemu::setPlain(proc.state(), "t1_machine", 0);
        const auto kick = dest ? dest->itemText(3) : juce::String("?");
        std::printf("lfo destination 3: on SD Vintage \"%s\", on Kick \"%s\" -> %s\n", sdv.toRawUTF8(),
                    kick.toRawUTF8(), sdv == "Freq Complex" && kick == "Shape" ? "PASS" : "FAIL");
        cyclesemu::setPlain(proc.state(), "t1_machine", machine0.getFloatValue());
    }

    // legato and portamento: Tone on track 1 (channel 1), the pitch word per
    // engine block, the block's peak and the frequency over a stretch
    {
        struct Run
        {
            std::vector<int> word, smooth;
            std::vector<float> peak, out;
        };
        auto play = [](std::initializer_list<std::pair<const char*, float>> settings,
                       std::vector<std::tuple<int, int, bool>> events, int blocks) {
            CyclesEmuProcessor f(Product::Full);
            cyclesemu::setPlain(f.state(), "midimode", 1);
            cyclesemu::setPlain(f.state(), "t1_machine", 4);
            for (auto* id : {"t1_color", "t1_shape", "t1_sweep", "t1_contour"})
                cyclesemu::setPlain(f.state(), id, 0.0f);
            cyclesemu::setPlain(f.state(), "t1_lfodepth", 64.0f);
            cyclesemu::setPlain(f.state(), "t1_decay", 127.0f);
            for (auto [id, v] : settings)
                cyclesemu::setPlain(f.state(), id, v);
            f.prepareToPlay(48000, 32);
            juce::AudioBuffer<float> buf(2, 32);
            Run r;
            const uint32_t pitchAt = f.kitAddress() + 0x1c + 0x28;
            for (int b = 0; b < 1500 + blocks; ++b)   // settled first, as elsewhere
            {
                juce::MidiBuffer midi;
                for (auto [at, note, on] : events)
                    if (at + 1500 == b)
                        midi.addEvent(on ? juce::MidiMessage::noteOn(1, note, (juce::uint8)110)
                                         : juce::MidiMessage::noteOff(1, note), 0);
                buf.clear();
                f.processBlock(buf, midi);
                if (b < 1500)
                    continue;
                r.word.push_back((f.peek(pitchAt) << 8) | f.peek(pitchAt + 1));
                r.smooth.push_back((f.peek(0x8000102eu) << 8) | f.peek(0x8000102fu));
                r.peak.push_back(buf.getMagnitude(0, 0, 32));
                for (int i = 0; i < 32; ++i)
                    r.out.push_back(buf.getSample(0, i));
            }
            return r;
        };
        // semitones from C4 over blocks [from, to): the mean period between
        // the first and last rising zero crossings, interpolated
        auto semis = [](const Run& r, int from, int to) {
            double first = -1, last = -1;
            int c = 0;
            for (int i = from * 32 + 1; i < to * 32; ++i)
            {
                const float a = r.out[(size_t)i - 1], b = r.out[(size_t)i];
                if (a < 0 && b >= 0)
                {
                    const double at = i - 1 + a / (a - b);
                    (first < 0 ? first : last) = at;
                    last = at;
                    ++c;
                }
            }
            return 12 * std::log2((c - 1) * 48000.0 / (last - first) / 261.63);
        };
        // portamento, constant time 200 ms (ptim 73): 60, then 72 a second later
        {
            const float ptim = (float)juce::roundToInt(127.0 * std::log(200.0) / std::log(10000.0));
            auto r = play({{"t1_port", 1}, {"t1_ptim", ptim}, {"t1_pslope", 1}},
                          {{0, 60, true}, {1500, 60, false}, {1500, 72, true}}, 2400);
            const int start = r.word[1500], mid = r.word[1500 + 150], end = r.word[1500 + 320];
            // where the voice starts: the smoothed word it reads, not the last note's
            const int startSmoothed = r.smooth[1500];
            const double first = semis(r, 1503, 1533);   // its first 20 ms, past the attack
            const double after = semis(r, 1500 + 400, 2400);
            const bool ok = start == 0x3400 + 0x0a && std::abs(mid - 0x3a00) < 0x30 && end == 0x4000
                            && std::abs(after - 12.0) < 0.1 && std::abs(startSmoothed - start) < 0x10
                            && std::abs(first) < 1.0;
            std::printf("portamento 200 ms: word 0x%04x at the note (voice reads 0x%04x, %.2f st in its first 20 ms), "
                        "0x%04x at 100 ms, 0x%04x at 213 ms, then %.2f st -> %s\n", start, startSmoothed, first, mid, end,
                        after, ok ? "PASS" : "FAIL");
        }
        // constant rate: 200 ms an octave, so two octaves take 400 ms
        {
            auto r = play({{"t1_port", 1}, {"t1_ptim", 73}, {"t1_pslope", 0}},
                          {{0, 48, true}, {1500, 48, false}, {1500, 72, true}}, 2400);
            int done = -1;
            for (int b = 1500; b < (int)r.word.size() && done < 0; ++b)
                if (r.word[b] == 0x4000)
                    done = b - 1500;
            std::printf("portamento, constant rate: two octaves in %.0f ms -> %s\n", done * 32 / 48.0,
                        std::abs(done * 32 / 48.0 - 2 * cyclesemu::glideMs(73)) < 3 ? "PASS" : "FAIL");
        }
        // glissando: semitone steps only
        {
            auto r = play({{"t1_port", 1}, {"t1_ptim", 80}, {"t1_pslope", 1}, {"t1_pstyle", 1}},
                          {{0, 60, true}, {1500, 60, false}, {1500, 67, true}}, 2400);
            std::set<int> words(r.word.begin() + 1500, r.word.end());
            const bool steps = std::all_of(words.begin(), words.end(), [](int w) { return (w & 0xff) == 0; });
            std::printf("glissando: %d pitches, all whole semitones -> %s\n", (int)words.size(),
                        steps && words.size() == 8 ? "PASS" : "FAIL");
        }
        // legato: 64 pressed while 60 is held moves the pitch without a new
        // note (no new attack); with legato off it attacks again
        {
            auto attack = [&](bool legato) {
                auto r = play({{"t1_legato", legato ? 1.0f : 0.0f}, {"t1_decay", 50.0f}},
                              {{0, 60, true}, {600, 64, true}}, 1200);
                float before = 0, after = 0;
                for (int b = 560; b < 600; ++b)
                    before = std::max(before, r.peak[(size_t)b]);
                for (int b = 600; b < 640; ++b)
                    after = std::max(after, r.peak[(size_t)b]);
                return std::tuple{after / before, semis(r, 700, 1200), r.word[700]};
            };
            auto [legRise, legSemis, legWord] = attack(true);
            auto [retRise, retSemis, retWord] = attack(false);
            std::printf("legato: peak x%.2f after the second key (retriggered x%.2f), %.2f st (word 0x%04x); "
                        "retriggered %.2f st (0x%04x) -> %s\n", legRise, retRise, legSemis, legWord, retSemis, retWord,
                        legRise < 1.1f && retRise > 1.5f && std::abs(legSemis - 4) < 0.1 && legWord == 0x4400
                                && std::abs(retSemis - 4) < 0.1 && retWord == 0x4000
                            ? "PASS" : "FAIL");
        }
        // legato release: 60 held, 64 on top and let go, back to 60; then
        // after all are let go, 62 plays at its own pitch from its start
        {
            auto r = play({{"t1_legato", 1}},
                          {{0, 60, true}, {300, 64, true}, {600, 64, false}, {900, 60, false}, {1200, 62, true}}, 1800);
            const double back = semis(r, 700, 900), next = semis(r, 1203, 1233), later = semis(r, 1300, 1800);
            std::printf("legato release: back to %.2f st; then 62 at %.2f st in its first 20 ms, %.2f st later -> %s\n",
                        back, next, later,
                        std::abs(back) < 0.1 && std::abs(next - 2) < 0.1 && std::abs(later - 2) < 0.1 ? "PASS" : "FAIL");
        }
        // note priority Low: a higher key held over a lower one does not take over
        {
            auto r = play({{"t1_legato", 1}, {"t1_notepri", 1}}, {{0, 60, true}, {300, 67, true}}, 800);
            const double s = semis(r, 400, 800);
            std::printf("note priority low: %.2f st with 67 over 60 -> %s\n", s, std::abs(s) < 0.1 ? "PASS" : "FAIL");
        }
    }

    // the filter under a fast LFO (sine, full depth, from closed): how big
    // the output's steps are at block edges against inside blocks, the
    // firmware's block-stepped filter against the smooth one
    {
        auto zipper = [](bool smooth, int cut, int speed) {
            CyclesEmuProcessor f(Product::Full);
            f.setSmoothFilter(smooth);
            cyclesemu::setPlain(f.state(), "midimode", 1);
            cyclesemu::setPlain(f.state(), "t1_machine", 4);
            cyclesemu::setPlain(f.state(), "t1_decay", 127);
            cyclesemu::setPlain(f.state(), "t1_fcut", (float)cut);
            cyclesemu::setPlain(f.state(), "t1_freso", 0);
            cyclesemu::setPlain(f.state(), "t1_lfodest", 14);
            cyclesemu::setPlain(f.state(), "t1_lfodepth", 127);
            cyclesemu::setPlain(f.state(), "t1_lfospeed", (float)speed);
            cyclesemu::setPlain(f.state(), "t1_lfowave", 1);
            cyclesemu::setPlain(f.state(), "t1_lforeset", 0);
            f.prepareToPlay(48000, 32);
            juce::AudioBuffer<float> buf(2, 32);
            std::vector<float> out;
            for (int b = 0; b < 3000; ++b)
            {
                juce::MidiBuffer midi;
                if (b == 1000)
                    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)110), 0);
                buf.clear();
                f.processBlock(buf, midi);
                if (b > 1100)
                    for (int i = 0; i < 32; ++i)
                        out.push_back(buf.getSample(0, i));
            }
            double edge = 0, inner = 0, level = 0;
            int ne = 0, ni = 0;
            for (size_t i = 2; i < out.size(); ++i)
            {
                const double d2 = std::abs(out[i] - 2 * out[i - 1] + out[i - 2]);
                level = std::max(level, (double)std::abs(out[i]));
                if (i % 32 == 0)
                    edge += d2, ++ne;
                else if (i % 32 > 2)
                    inner += d2, ++ni;
            }
            return std::pair{(edge / ne) / (inner / ni), level};
        };
        bool ok = true;
        for (int cut : {0, 32, 64, 100})
        {
            std::printf("filter lfo, cutoff %d: edge/inner steps", cut);
            for (int speed : {80, 127})
            {
                auto [fw, fwLevel] = zipper(false, cut, speed);
                auto [sm, smLevel] = zipper(true, cut, speed);
                std::printf("  speed %d: firmware %.2f, smooth %.2f (peak %.3f vs %.3f)", speed, fw, sm, fwLevel, smLevel);
                ok &= sm < 1.3;
            }
            std::printf("\n");
        }
        std::printf("filter lfo smooth -> %s\n", ok ? "PASS" : "FAIL");
    }

    // the Digitone II's filter types: the engine's audio against its own
    // law (mc_filter_db, which the filter screen draws). Metal (broadband)
    // on track 1, rendered unfiltered and filtered; the response is their
    // cross-spectrum over the plain one, compared where Metal has energy
    {
        auto render = [](int type, int cut, int res, int extra) {
            CyclesEmuProcessor f(Product::Full);
            cyclesemu::setPlain(f.state(), "midimode", 1);
            cyclesemu::setPlain(f.state(), "t1_machine", 2);
            cyclesemu::setPlain(f.state(), "t1_decay", 127);
            cyclesemu::setPlain(f.state(), "t1_lfodepth", 64);
            cyclesemu::setPlain(f.state(), "t1_delaysend", 0);
            cyclesemu::setPlain(f.state(), "t1_reverbsend", 0);
            cyclesemu::setPlain(f.state(), "t1_voldist", 40);
            cyclesemu::setPlain(f.state(), "t1_fenv", 64);
            cyclesemu::setPlain(f.state(), "t1_ftype", (float)type);
            cyclesemu::setPlain(f.state(), "t1_fcut", (float)cut);
            cyclesemu::setPlain(f.state(), "t1_freso", (float)res);
            cyclesemu::setPlain(f.state(), "t1_fextra", (float)extra);
            f.prepareToPlay(48000, 32);
            juce::AudioBuffer<float> buf(2, 32);
            std::vector<float> out;
            for (int b = 0; b < 1500 + 1600; ++b)
            {
                juce::MidiBuffer midi;
                if (b == 1500)
                    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)110), 0);
                buf.clear();
                f.processBlock(buf, midi);
                if (b >= 1520)
                    for (int i = 0; i < 32; ++i)
                        out.push_back(buf.getSample(0, i) + buf.getSample(1, i));
            }
            return out;
        };
        const auto dry = render(0, 64, 0, 0);
        constexpr int order = 12, n = 1 << order;
        juce::dsp::FFT fft(order);
        // averaged cross- and auto-spectra, Hann windows, half overlap
        auto spectra = [&](const std::vector<float>& y, std::vector<std::complex<double>>& sxy, std::vector<double>& sxx) {
            sxy.assign(n / 2, {});
            sxx.assign(n / 2, 0);
            std::vector<std::complex<float>> a(n), b(n), A(n), B(n);
            for (size_t at = 0; at + n <= dry.size(); at += n / 2)
            {
                for (int i = 0; i < n; ++i)
                {
                    const float w = 0.5f - 0.5f * std::cos(2 * juce::MathConstants<float>::pi * i / n);
                    a[(size_t)i] = dry[at + (size_t)i] * w;
                    b[(size_t)i] = y[at + (size_t)i] * w;
                }
                fft.perform(a.data(), A.data(), false);
                fft.perform(b.data(), B.data(), false);
                for (int k = 0; k < n / 2; ++k)
                {
                    sxy[(size_t)k] += std::complex<double>(B[(size_t)k]) * std::conj(std::complex<double>(A[(size_t)k]));
                    sxx[(size_t)k] += std::norm(std::complex<double>(A[(size_t)k]));
                }
            }
        };
        struct Case { int type, cut, res, extra; const char* name; };
        const Case cases[] = {{1, 72, 64, 0, "Multimode LP"}, {1, 72, 64, 64, "Multimode BP"}, {1, 80, 30, 127, "Multimode HP"},
                              {2, 72, 100, 0, "Lowpass 4"}, {3, 80, 127, 90, "Equalizer +12"}, {3, 80, 0, 64, "Equalizer -12"},
                              {4, 72, 64, 0, "Legacy LP"}, {4, 80, 64, 127, "Legacy HP"}, {6, 72, 80, 127, "Comb+"},
                              {5, 72, 80, 127, "Comb-"}};
        bool ok = true;
        for (const auto& c : cases)
        {
            const auto wet = render(c.type, c.cut, c.res, c.extra);
            std::vector<std::complex<double>> sxy;
            std::vector<double> sxx;
            spectra(wet, sxy, sxx);
            std::vector<double> sorted(sxx.begin(), sxx.end());
            std::nth_element(sorted.begin(), sorted.begin() + (long)sorted.size() / 2, sorted.end());
            const double floor = sorted[sorted.size() / 2];
            std::vector<double> errs;
            for (int k = 1; k < n / 2; ++k)
            {
                const double hz = k * 48000.0 / n;
                if (hz < 60 || hz > 12000 || sxx[(size_t)k] < floor)
                    continue;
                const double measured = 20 * std::log10(std::abs(sxy[(size_t)k] / sxx[(size_t)k]) + 1e-12);
                errs.push_back(std::abs(measured - mc_filter_db(cyclesemu::filterEngineType(c.type), c.cut, c.res, c.extra, hz)));
            }
            std::sort(errs.begin(), errs.end());
            const double median = errs[errs.size() / 2], p90 = errs[errs.size() * 9 / 10];
            // the combs' deep notches are where a measurement over a decaying
            // sound is least sure: a looser 90th percentile for them
            const bool comb = c.type == 5 || c.type == 6;
            const bool good = median < 0.5 && p90 < (comb ? 3.0 : 2.0);
            ok &= good;
            std::printf("filter %-14s engine against its law: median %.2f dB, 90%% within %.2f dB%s\n", c.name, median, p90,
                        good ? "" : "  <-");
        }
        std::printf("filter types: the engine follows the drawn response -> %s\n", ok ? "PASS" : "FAIL");
    }

    // the tab lights: a drum-map note for track 3 lights its tab (and only
    // its), the page stays put, and the light goes out after 150 ms
    {
        proc.prepareToPlay(48000, 128);
        juce::AudioBuffer<float> buf(2, 128);
        e->showPage(0);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 38, (juce::uint8)100), 0);   // track 3
        buf.clear();
        proc.processBlock(buf, midi);
        e->updateTrigLights();
        const bool stayed = e->currentPage() == 0, lit = e->tabLit(2), otherDark = !e->tabLit(0);
        {
            auto img = e->createComponentSnapshot(e->getLocalBounds().withHeight(220), true, 1.0f);
            auto f = out.getChildFile("editor-tab-lit.png");
            f.deleteFile();
            juce::FileOutputStream os(f);
            juce::PNGImageFormat().writeImageToStream(img, os);
        }
        juce::Thread::sleep(300);
        e->updateTrigLights();
        const bool unlit = !e->tabLit(2);
        std::printf("tab lights: a note on track 3 lights its tab %s (track 1's dark %s), the page stays %s, out after "
                    "150 ms %s -> %s\n", lit ? "yes" : "no", otherDark ? "yes" : "no", stayed ? "yes" : "no",
                    unlit ? "yes" : "no", lit && otherDark && stayed && unlit ? "PASS" : "FAIL");
    }

    // the meter eases with the sound: a held Tone, level 127 -> 64; each
    // block, the meter (over its level-127 value) against the output (over
    // a render that stays at 127): the firmware's own fade
    {
        auto run = [](bool step, std::vector<double>& meter) {
            CyclesEmuProcessor f(Product::Full);
            cyclesemu::setPlain(f.state(), "midimode", 1);
            cyclesemu::setPlain(f.state(), "t1_machine", 4);
            cyclesemu::setPlain(f.state(), "t1_decay", 127);
            cyclesemu::setPlain(f.state(), "t1_lfodepth", 64);
            cyclesemu::setPlain(f.state(), "t1_delaysend", 0);
            cyclesemu::setPlain(f.state(), "t1_reverbsend", 0);
            cyclesemu::setPlain(f.state(), "t1_level", 127);
            f.prepareToPlay(48000, 32);
            juce::AudioBuffer<float> buf(2, 32);
            std::vector<double> e;
            for (int b = 0; b < 2000 + 120; ++b)
            {
                juce::MidiBuffer midi;
                if (b == 1500)
                    midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)110), 0);
                if (b == 2000 && step)
                    cyclesemu::setPlain(f.state(), "t1_level", 64);
                buf.clear();
                f.processBlock(buf, midi);
                const float pk = f.takeTrackPeak(0);
                if (b >= 2000)
                {
                    double s2 = 0;
                    for (int i = 0; i < 32; ++i)
                        s2 += (double)buf.getSample(0, i) * buf.getSample(0, i) + (double)buf.getSample(1, i) * buf.getSample(1, i);
                    e.push_back(std::sqrt(s2));
                    meter.push_back(pk);
                }
            }
            return e;
        };
        std::vector<double> m0, m1;
        const auto ref = run(false, m0), st = run(true, m1);
        double worst = 0, biggestStep = 0;
        for (size_t k = 0; k < st.size(); ++k)
        {
            worst = std::max(worst, std::abs(m1[k] / m0[k] - st[k] / ref[k]));
            if (k > 0)
                biggestStep = std::max(biggestStep, std::abs(m1[k] / m0[k] - m1[k - 1] / m0[k - 1]));
        }
        std::printf("meter eases with the level: off the sound's gain by at most %.3f, at most %.3f a block -> %s\n", worst,
                    biggestStep, worst < 0.03 && biggestStep < 0.03 ? "PASS" : "FAIL");
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
