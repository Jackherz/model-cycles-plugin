#include <cstring>
#include <complex>
#include <juce_dsp/juce_dsp.h>
#include <set>
// Render every page of the CyclesEmu editor to PNG (layout check).
//   CyclesEmuEditorShot OUT_DIR
#include "../Source/PluginEditor.h"
#include "../Source/Fanfare.h"
#include "../Source/Help.h"
#include "../Source/TwinSamples.h"
#include <map>

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::File out(argc > 1 ? argv[1] : ".");
    CyclesEmuProcessor proc;
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    auto* e = dynamic_cast<CyclesEmuEditor*>(ed.get());
    const char* names[] = {"track1", "track2", "track3", "track4", "track5", "track6", "track7", "track8", "mix", "fx"};
    for (int i = 0; i < cyclesemu::kTracks + 2; ++i)
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
        cyclesemu::setPlain(proc.state(), "t1_machine", 11);   // the closed list's text follows too
        const auto twinShown = dest ? dest->shownText() : juce::String("?");
        cyclesemu::setPlain(proc.state(), "t1_machine", 0);
        const auto kick = dest ? dest->itemText(3) : juce::String("?");
        std::printf("lfo destination 3: on SD Vintage \"%s\", on Kick \"%s\", SP Twinshot shows \"%s\" -> %s\n",
                    sdv.toRawUTF8(), kick.toRawUTF8(), twinShown.toRawUTF8(),
                    sdv == "Freq Complex" && kick == "Shape" && twinShown == "Filter 1" ? "PASS" : "FAIL");
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
            const bool good = median < 0.5 && p90 < (comb ? 3.5 : 2.0);
            ok &= good;
            std::printf("filter %-14s engine against its law: median %.2f dB, 90%% within %.2f dB%s\n", c.name, median, p90,
                        good ? "" : "  <-");
        }
        std::printf("filter types: the engine follows the drawn response -> %s\n", ok ? "PASS" : "FAIL");
        // the combs in tune high up: Comb+ at FREQ 108 (C7, 2093 Hz, a
        // period of 22.93 samples), full feedback so it rings: the period
        // from the output's autocorrelation, interpolated
        {
            const auto wet = render(6, 108, 127, 127);
            const size_t from = wet.size() / 4, len = wet.size() / 2;
            auto ac = [&](int lag) {
                double acc = 0;
                for (size_t i = from; i < from + len; ++i)
                    acc += (double)wet[i] * wet[i + (size_t)lag];
                return acc;
            };
            int at = 15;
            for (int lag = 15; lag <= 30; ++lag)
                if (ac(lag) > ac(at))
                    at = lag;
            const double a0 = ac(at - 1), b0 = ac(at), c0 = ac(at + 1);
            const double period = at + 0.5 * (a0 - c0) / (a0 - 2 * b0 + c0);
            const double want = 48000.0 / (261.6256 * std::pow(2.0, (108 - 72) / 12.0));
            const double cents = 1200.0 * std::log2(want / period);
            float peakOut = 0;
            for (float x : wet)
                peakOut = std::max(peakOut, std::isfinite(x) ? std::abs(x) : 1e9f);
            std::printf("comb tuning: Comb+ at C7 rings with a period of %.3f samples (wanted %.3f: %+.1f cents), "
                        "full feedback bounded at %.3f -> %s\n", period, want, cents, peakOut,
                        std::abs(cents) < 5.0 && peakOut < 2.0f ? "PASS" : "FAIL");
        }
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

    // the Syntakt's SOUND SETUP modulation: Velocity Mod and Key Tracking
    // move their destinations' engine words at each note; the knob keeps
    // the offset; Velocity To Vol; Filter Keytrack
    {
        // a note's sustained level (RMS after the attack)
        auto playNote = [](CyclesEmuProcessor& f, int note, int vel) {
            juce::AudioBuffer<float> buf(2, 32);
            double e = 0;
            for (int b = 0; b < 300; ++b)
            {
                juce::MidiBuffer midi;
                if (b == 0)
                    midi.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8)vel), 0);
                if (b == 299)
                    midi.addEvent(juce::MidiMessage::noteOff(1, note), 0);
                buf.clear();
                f.processBlock(buf, midi);
                if (b >= 100 && b < 299)
                    for (int i = 0; i < 32; ++i)
                        e += (double)buf.getSample(0, i) * buf.getSample(0, i);
            }
            return (float)std::sqrt(e / (199 * 32));
        };
        auto fresh = [](std::initializer_list<std::pair<const char*, float>> settings) {
            auto f = std::make_unique<CyclesEmuProcessor>(Product::Full);
            cyclesemu::setPlain(f->state(), "midimode", 1);
            cyclesemu::setPlain(f->state(), "t1_machine", 4);
            cyclesemu::setPlain(f->state(), "t1_decay", 127);
            cyclesemu::setPlain(f->state(), "t1_lfodepth", 64);
            for (auto [id, v] : settings)
                cyclesemu::setPlain(f->state(), id, v);
            f->prepareToPlay(48000, 32);
            juce::AudioBuffer<float> buf(2, 32);
            juce::MidiBuffer none;
            for (int b = 0; b < 1500; ++b)
            {
                buf.clear();
                f->processBlock(buf, none);
            }
            return f;
        };
        // velocity: Color (knob 20) + 40 x vel/127; key: Shape (knob 64) + 16/8 = 2 a semitone from C
        auto f = fresh({{"t1_color", 20}, {"t1_shape", 64}, {"t1_vm1dst", 2}, {"t1_vm1dep", 40}, {"t1_km1dst", 3},
                        {"t1_km1dep", 16}});
        playNote(*f, 60, 127);
        const int c127 = f->engineValue(0, 0x2a), s60 = f->engineValue(0, 0x2c);
        playNote(*f, 72, 64);
        const int c64 = f->engineValue(0, 0x2a), s72 = f->engineValue(0, 0x2c);
        cyclesemu::setPlain(f->state(), "t1_color", 30);   // the knob moves: the offset stays
        playNote(*f, 48, 64);   // and a note after: Shape 64 - 24
        const int cKnob = f->engineValue(0, 0x2a), s48 = f->engineValue(0, 0x2c);
        const bool modOk = c127 == 60 && s60 == 64 && c64 == 40 && s72 == 88 && cKnob == 50 && s48 == 40;
        std::printf("velocity mod / key tracking: Color 20+40 at vel 127 -> %d, vel 64 -> %d, knob 30 -> %d; Shape 64 "
                    "+2/semitone: C5 %d, +12 %d, -12 %d -> %s\n", c127, c64, cKnob, s60, s72, s48, modOk ? "PASS" : "FAIL");
        // Velocity To Vol: Off plays soft notes at full level; Exp quieter than Lin
        auto lin = fresh({}), off = fresh({{"t1_velvol", 0}}), ex = fresh({{"t1_velvol", 3}});
        const float l32 = playNote(*lin, 60, 32), l127 = playNote(*lin, 60, 127);
        const float o32 = playNote(*off, 60, 32), e32 = playNote(*ex, 60, 32);
        const bool volOk = std::abs(o32 / l127 - 1.0f) < 0.05f && e32 < l32 * 0.5f && l32 < l127 * 0.5f;
        std::printf("velocity to vol: vel 32 / vel 127 (Lin) %.3f, Off %.3f, Exp %.3f -> %s\n", l32 / l127, o32 / l127,
                    e32 / l127, volOk ? "PASS" : "FAIL");
        // Filter Keytrack: Multimode LP low (FREQ 48, about 117 Hz); C7 against C5
        auto ratio = [&](float key) {
            auto g = fresh({{"t1_ftype", 1}, {"t1_fcut", 48}, {"t1_freso", 0}, {"t1_fextra", 0}, {"t1_fkey", key}});
            const float lo = playNote(*g, 60, 100), hi = playNote(*g, 84, 100);
            return hi / lo;
        };
        const float none = ratio(0), full = ratio(127);
        std::printf("filter keytrack: C7 over C5 through a low LP: keytrack 0 %.3f, 127 %.3f -> %s\n", none, full,
                    full > none * 4 ? "PASS" : "FAIL");
    }

    // eight tracks (NOTES.md step 46): tracks 7-8 on a second engine,
    // sharing the first's delay and reverb
    {
        struct Ev { int block, channel, note, vel; };
        auto render = [](Product product, std::initializer_list<std::pair<const char*, float>> settings, std::vector<Ev> events,
                         int blocks, int mode = 1) {
            auto f = std::make_unique<CyclesEmuProcessor>(product);
            cyclesemu::setPlain(f->state(), "midimode", (float)mode);
            for (auto [id, v] : settings)
                cyclesemu::setPlain(f->state(), id, v);
            f->prepareToPlay(48000, 32);
            juce::AudioBuffer<float> buf(2, 32);
            std::vector<float> out;
            std::vector<float> peaks(cyclesemu::kTracks, 0.0f);
            for (int b = 0; b < blocks; ++b)
            {
                juce::MidiBuffer midi;
                for (auto& e : events)
                    if (e.block == b)
                        midi.addEvent(e.vel ? juce::MidiMessage::noteOn(e.channel, e.note, (juce::uint8)e.vel)
                                            : juce::MidiMessage::noteOff(e.channel, e.note), 0);
                buf.clear();
                f->processBlock(buf, midi);
                for (int i = 0; i < 32; ++i)
                    out.push_back(buf.getSample(0, i)), out.push_back(buf.getSample(1, i));
                for (int t = 0; t < cyclesemu::kTracks; ++t)
                    peaks[(size_t)t] = std::max(peaks[(size_t)t], f->takeTrackPeak(t));
            }
            return std::pair{out, peaks};
        };
        auto rms = [](const std::vector<float>& x, size_t from, size_t to) {
            double e = 0;
            for (size_t i = from; i < to; ++i)
                e += x[i] * x[i];
            return std::sqrt(e / (double)(to - from));
        };
        // track 1 against track 7, the same sound (the same kit track), both sends up: dry, delay and
        // reverb through the shared effects. (A send hot enough to saturate the firmware delay's
        // feedback loop, 100 at full level with feedback 70, amplifies the shared path's 1-bit
        // difference into another, equally valid echo tail: reported, not tested; step 46.)
        const int w0 = 400;   // the sends ramp to their settings first
        auto pair17 = [&](float send, float fb) {
            std::initializer_list<std::pair<const char*, float>> same{
                {"t1_machine", 4}, {"t1_decay", 90}, {"t1_delaysend", send}, {"t1_reverbsend", send}, {"t1_pan", 64},
                {"t7_machine", 4}, {"t7_decay", 90}, {"t7_delaysend", send}, {"t7_reverbsend", send}, {"t7_pan", 64},
                {"t7_level", 100}, {"t1_level", 100}, {"fx_delayfeedback", fb}};
            auto on1 = render(Product::Full, same, {{w0, 1, 60, 110}}, w0 + 3000).first;
            auto on7 = render(Product::Full, same, {{w0, 7, 60, 110}}, w0 + 3000).first;
            double sum2 = 0, ref2 = 0;
            for (size_t i = (size_t)w0 * 64; i < on1.size(); ++i)
                sum2 += (on7[i] - on1[i]) * (on7[i] - on1[i]), ref2 += (double)on1[i] * on1[i];
            return 10 * std::log10(sum2 / ref2 + 1e-30);
        };
        const double diffDb = pair17(60, 70), hotDb = pair17(100, 70);
        // the drum map: F#1 and G1 play tracks 7 and 8; channels 7 and 8 in Ch mode
        auto [dm, pdm] = render(Product::Full, {}, {{100, 1, 42, 110}, {100, 1, 43, 110}}, 400, 0);
        auto [chm, pch] = render(Product::Full, {}, {{100, 7, 60, 110}, {100, 8, 60, 110}}, 400, 1);
        // mute 7, solo 8, level 8 at 0
        auto [mu, pmu] = render(Product::Full, {{"t7_mute", 1}}, {{100, 7, 60, 110}}, 400);
        auto [so, pso] = render(Product::Full, {{"t8_solo", 1}}, {{100, 1, 60, 110}, {100, 8, 60, 110}}, 400);
        auto [lv, plv] = render(Product::Full, {{"t8_level", 0}}, {{100, 8, 60, 110}}, 400);
        const double one8 = rms(chm, 100 * 64, chm.size());
        // CyclesVoice: 8 notes, 8 voices, each heard
        std::vector<Ev> chord;
        for (int k = 0; k < 8; ++k)
            chord.push_back({100, 1, 48 + 3 * k, 110});
        auto [v8, pv8] = render(Product::Voice, {{"voices", 8}}, chord, 400);
        int voicesHeard = 0;
        for (float pk : pv8)
            voicesHeard += pk > 0.001f;
        // a kit saves 8 tracks; a 6-track kit loads, tracks 7-8 as they were
        bool kitOk = false;
        {
            CyclesEmuProcessor f(Product::Full);
            cyclesemu::setPlain(f.state(), "t8_decay", 33);
            auto kit = cyclesemu::kitToTree(f.state());
            int tracks = 0;
            for (auto tr : kit)
                tracks += tr.hasType("Track");
            for (int i = kit.getNumChildren() - 1; i >= 0; --i)   // the kit as six tracks had it
                if (kit.getChild(i).hasType("Track") && (int)kit.getChild(i).getProperty("index") >= 6)
                    kit.removeChild(i, nullptr);
            cyclesemu::setPlain(f.state(), "t8_decay", 99);
            cyclesemu::treeToKit(f.state(), kit);
            kitOk = tracks == 8 && juce::roundToInt(f.state().getRawParameterValue("t8_decay")->load()) == 99;
        }
        // the amp envelope and LFO 2 on track 8
        auto [env8, pe8] = render(Product::Full, {{"t8_machine", 4}, {"t8_decay", 127}, {"t8_amp", 1}, {"t8_amode", 0}, {"t8_ahold", 0},
                                                  {"t8_adec", 8}, {"t8_delaysend", 0}, {"t8_reverbsend", 0}}, {{400, 8, 60, 110}}, 1200);
        auto [ref8, pr8] = render(Product::Full, {{"t8_machine", 4}, {"t8_decay", 127}, {"t8_delaysend", 0}, {"t8_reverbsend", 0}},
                                  {{400, 8, 60, 110}}, 1200);
        const double envRatio = rms(env8, 900 * 64, 1200 * 64) / (rms(ref8, 900 * 64, 1200 * 64) + 1e-12);
        // SP Twinshot on track 7: the samples are in the second engine too
        auto [tw7, ptw7] = render(Product::Full, {{"t7_machine", 11}, {"t7_decay", 127}}, {{400, 7, 60, 110}}, 900);
        const bool twinLoaded = CyclesEmuProcessor(Product::Full).twinSamples().count() > 0;
        const double twin7 = rms(tw7, 400 * 64, 900 * 64);
        const bool ok = diffDb < -100 && pdm[6] > 0.001f && pdm[7] > 0.001f
                        && pch[6] > 0.001f && pch[7] > 0.001f && pmu[6] < 0.001f && pso[0] < 0.001f && pso[7] > 0.001f
                        && plv[7] < 0.001f && one8 > 0.0 && voicesHeard == 8 && kitOk && envRatio < 0.01
                        && (!twinLoaded || twin7 > 0.001);
        std::printf("eight tracks: track 7 against track 1, the same sound, delay and reverb sends 60: difference %.1f dB "
                    "(sends 100 at feedback 70, the delay saturating: %.1f dB); drum map F#1 G1 -> tracks 7 8 (%.3f %.3f); Ch 7 8 (%.3f %.3f); mute 7 %.3f; "
                    "solo 8: track 1 %.3f, 8 %.3f; level 8 at 0 %.3f; CyclesVoice 8 notes: %d voices heard; kits: 8 tracks, a "
                    "6-track kit leaves 7-8 %d; amp envelope on track 8 %.4f; SP Twinshot on track 7 %.4f (samples %d) -> %s\n",
                    diffDb, hotDb, pdm[6], pdm[7], pch[6], pch[7], pmu[6], pso[0], pso[7], plv[7], voicesHeard,
                    kitOk, envRatio, twin7, twinLoaded, ok ? "PASS" : "FAIL");
    }

    // LFO 2 (NOTES.md step 45): off is the old sound; at LFO 1's settings
    // it moves a word as LFO 1 does (the smoothed words the voices read)
    {
        struct Ev { int block, note; };
        auto words = [](std::initializer_list<std::pair<const char*, float>> settings, std::vector<Ev> notes, int blocks,
                        int k, double bpm = 120.0, std::vector<std::pair<int, std::pair<const char*, float>>> changes = {}) {
            CyclesEmuProcessor f(Product::Full);
            cyclesemu::setPlain(f.state(), "midimode", 1);
            cyclesemu::setPlain(f.state(), "followhost", 0);
            cyclesemu::setPlain(f.state(), "tempo", (float)bpm);
            cyclesemu::setPlain(f.state(), "t1_machine", 4);
            cyclesemu::setPlain(f.state(), "t1_decay", 127);
            cyclesemu::setPlain(f.state(), "t1_lfodepth", 64);
            for (auto [id, v] : settings)
                cyclesemu::setPlain(f.state(), id, v);
            f.prepareToPlay(48000, 32);
            juce::AudioBuffer<float> buf(2, 32);
            std::vector<double> w;
            std::vector<float> audio;
            const uint32_t a = 0x8000100cu + 14u + 2u * (uint32_t)k;
            for (int b = 0; b < blocks; ++b)
            {
                for (auto& c : changes)
                    if (c.first == b)
                        cyclesemu::setPlain(f.state(), c.second.first, c.second.second);
                juce::MidiBuffer midi;
                for (auto& e : notes)
                    if (e.block == b)
                        midi.addEvent(juce::MidiMessage::noteOn(1, e.note, (juce::uint8)110), 0);
                buf.clear();
                f.processBlock(buf, midi);
                w.push_back((int16_t)((f.peek(a) << 8) | f.peek(a + 1)) / 256.0);
                for (int i = 0; i < 32; ++i)
                    audio.push_back(buf.getSample(0, i));
            }
            return std::pair{w, audio};
        };
        auto range = [](const std::vector<double>& w, int from, int to) {
            auto [lo, hi] = std::minmax_element(w.begin() + from, w.begin() + to);
            return std::pair{*lo, *hi};
        };
        const std::vector<Ev> note{{100, 60}};
        const int pan = 22, kCut = 28;
        // off: its settings changed, destination Off, the sound is the same
        auto [ref, refAudio] = words({}, note, 3000, pan);
        auto [off, offAudio] = words({{"t1_l2speed", 120}, {"t1_l2depth", 100}, {"t1_l2wave", 2}}, note, 3000, pan);
        float bypass = 0;
        for (size_t i = 0; i < refAudio.size(); ++i)
            bypass = std::max(bypass, std::abs(refAudio[i] - offAudio[i]));
        // LFO 1 against LFO 2 on pan, each waveform (not Random: its values are its own): speed +32,
        // x4 (a cycle a bar), depth +16, Trig; the note after the settings settle (block 1500)
        const std::vector<Ev> late{{1500, 60}};
        double maxDiff = 0, lo2 = 1e9, hi2 = -1e9;
        int within = 0, compared = 0;
        for (int wave = 0; wave < 6; ++wave)
        {
            auto [l1, a1] = words({{"t1_lfodest", 13}, {"t1_lfowave", (float)wave}, {"t1_lforeset", 1}, {"t1_lfosync", 0},
                                   {"t1_lfomult", 2}, {"t1_lfospeed", 96}, {"t1_lfodepth", 80}, {"t1_lfophase", 0}}, late, 4600, pan);
            auto [l2, a2] = words({{"t1_l2dest", 8}, {"t1_l2wave", (float)wave}, {"t1_l2reset", 1}, {"t1_l2sync", 0},
                                   {"t1_l2mult", 2}, {"t1_l2speed", 96}, {"t1_l2depth", 80}, {"t1_l2phase", 0}}, late, 4600, pan);
            for (int b = 1520; b < 4600; ++b)
            {
                const double d = std::abs(l1[(size_t)b] - l2[(size_t)b]);
                within += d <= 1.0;
                ++compared;
                if (wave < 2)   // the continuous ones: everywhere
                    maxDiff = std::max(maxDiff, d);
            }
            if (wave == 0)
                std::tie(lo2, hi2) = range(l2, 1520, 4600);
        }
        const double share = (double)within / compared;
        // both on pan, in phase: they add (depth +8 each: +-16 and +-16)
        auto [both, ab] = words({{"t1_lfodest", 13}, {"t1_lfowave", 0}, {"t1_lforeset", 1}, {"t1_lfosync", 0}, {"t1_lfofade", 64},
                                 {"t1_lfophase", 0}, {"t1_lfomult", 2}, {"t1_lfospeed", 96}, {"t1_lfodepth", 72},
                                 {"t1_l2dest", 8}, {"t1_l2reset", 1}, {"t1_l2mult", 2}, {"t1_l2speed", 96}, {"t1_l2depth", 72}},
                                late, 4600, pan);
        auto [loB, hiB] = range(both, 1520, 4600);
        // trig modes: Hold freezes at the note; One stops after a cycle, Half after half (a cycle = 3000 blocks)
        auto mode = [&](int m) {
            return words({{"t1_l2dest", 8}, {"t1_l2wave", 0}, {"t1_l2reset", (float)m}, {"t1_l2mult", 2}, {"t1_l2speed", 96},
                          {"t1_l2depth", 80}}, {{1000, 60}}, 6000, pan).first;
        };
        auto hold = mode(2), one = mode(3), half = mode(4);
        auto [hLo, hHi] = range(hold, 1500, 6000);
        auto [oLo, oHi] = range(one, 4200, 6000);
        auto [fLo, fHi] = range(half, 2700, 6000);
        // tempo: synced, twice the tempo is twice the rate; free, the same
        auto cycles = [&](float sync, double bpm) {
            auto w = words({{"t1_l2dest", 8}, {"t1_l2wave", 2}, {"t1_l2sync", sync}, {"t1_l2mult", 2}, {"t1_l2speed", 96},
                            {"t1_l2depth", 80}}, {}, 24000, pan, bpm).first;   // 16 s
            int flips = 0;
            for (size_t b = 1; b < w.size(); ++b)
                flips += (w[b] > 64.0) != (w[b - 1] > 64.0);
            return flips;
        };
        const int sync120 = cycles(0, 120), sync240 = cycles(0, 240), free240 = cycles(1, 240);
        // the knob moves while it runs: the centre follows the knob
        auto [moved, am] = words({{"t1_l2dest", 8}, {"t1_l2mult", 2}, {"t1_l2speed", 96}, {"t1_l2depth", 72}}, note, 6000, pan,
                                 120.0, {{3000, {"t1_pan", 32}}});
        auto [mLo, mHi] = range(moved, 3100, 6000);
        // Pitch: semitones, 2 a depth step
        auto [pitch, ap] = words({{"t1_l2dest", 1}, {"t1_l2mult", 2}, {"t1_l2speed", 96}, {"t1_l2depth", 66}}, note, 3000, 10);
        auto [pLo, pHi] = range(pitch, 150, 3000);
        // fast on the cutoff (x64, 16 cycles a bar): the word's largest step a block
        auto [cut, ac] = words({{"t1_fcut", 64}, {"t1_l2dest", 11}, {"t1_l2mult", 6}, {"t1_l2speed", 127}, {"t1_l2depth", 96}},
                               note, 3000, kCut);
        double maxStep = 0;
        for (size_t b = 151; b < 3000; ++b)
            maxStep = std::max(maxStep, std::abs(cut[b] - cut[b - 1]));
        // its cost: all 6 tracks with LFO 2 on (fast, on the cutoff) against all off, 6 notes playing
        auto timed = [](bool on) {
            CyclesEmuProcessor f(Product::Full);
            cyclesemu::setPlain(f.state(), "midimode", 1);
            for (int t = 1; t <= 6; ++t)
            {
                const juce::String p = "t" + juce::String(t) + "_";
                cyclesemu::setPlain(f.state(), p + "decay", 127);
                if (on)
                    for (auto [id, v] : {std::pair<const char*, float>{"l2dest", 11}, {"l2mult", 6}, {"l2speed", 120}, {"l2depth", 90}})
                        cyclesemu::setPlain(f.state(), p + id, v);
            }
            f.prepareToPlay(48000, 128);
            juce::AudioBuffer<float> buf(2, 128);
            double best = 1e9;
            for (int rep = 0; rep < 3; ++rep)
            {
                const auto t0 = juce::Time::getHighResolutionTicks();
                for (int b = 0; b < 1500; ++b)
                {
                    juce::MidiBuffer midi;
                    if (b % 375 == 0)
                        for (int ch = 1; ch <= 6; ++ch)
                            midi.addEvent(juce::MidiMessage::noteOn(ch, 60, (juce::uint8)110), 0);
                    buf.clear();
                    f.processBlock(buf, midi);
                }
                best = std::min(best, juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - t0) / 1500);
            }
            return best * 1e6;
        };
        const double usOff = timed(false), usOn = timed(true);
        {   // eight tracks playing: the cost of tracks 7-8 (the second engine) on top
            CyclesEmuProcessor f(Product::Full);
            cyclesemu::setPlain(f.state(), "midimode", 1);
            for (int t = 1; t <= cyclesemu::kTracks; ++t)
                cyclesemu::setPlain(f.state(), ("t" + juce::String(t) + "_decay").toRawUTF8(), 127);
            f.prepareToPlay(48000, 128);
            juce::AudioBuffer<float> buf(2, 128);
            double best = 1e9;
            for (int rep = 0; rep < 3; ++rep)
            {
                const auto t0 = juce::Time::getHighResolutionTicks();
                for (int b = 0; b < 1500; ++b)
                {
                    juce::MidiBuffer midi;
                    if (b % 375 == 0)
                        for (int ch = 1; ch <= cyclesemu::kTracks; ++ch)
                            midi.addEvent(juce::MidiMessage::noteOn(ch, 60, (juce::uint8)110), 0);
                    buf.clear();
                    f.processBlock(buf, midi);
                }
                best = std::min(best, juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - t0) / 1500);
            }
            std::printf("eight tracks cost: 128-sample callback, 8 tracks playing %.1f us (%.1f%% of real time); 6 playing %.1f us\n",
                        best * 1e6, best * 1e6 / (128.0 / 48000.0 * 1e6) * 100, usOff);
        }
        std::printf("lfo 2 cost: 128-sample callback, 6 tracks playing: off %.1f us, LFO 2 on all 6 %.1f us (%+.1f us, %.2f%% of "
                    "real time)\n", usOff, usOn, usOn - usOff, (usOn - usOff) / (128.0 / 48000.0 * 1e6) * 100);
        // a track preset saved before LFO 2 loads with it off
        bool presetOff = false;
        {
            CyclesEmuProcessor f(Product::Full);
            auto tree = cyclesemu::trackToTree(f.state(), 0);
            tree.removeProperty("l2dest", nullptr);
            cyclesemu::setPlain(f.state(), "t1_l2dest", 8);
            cyclesemu::treeToTrack(f.state(), 0, tree);
            presetOff = f.state().getRawParameterValue("t1_l2dest")->load() < 0.5f;
        }
        const bool ok = bypass == 0.0f && maxDiff <= 1.0 && share > 0.97 && std::abs(lo2 - 32.0) < 1.0 && std::abs(hi2 - 96.0) < 1.0
                        && std::abs(loB - 32.0) < 1.0 && std::abs(hiB - 96.0) < 1.0   // depth +8 each: +-16 + +-16 && hHi - hLo < 0.1 && oHi - oLo < 0.01 && fHi - fLo < 0.01
                        && std::abs(sync120 - 16) <= 1 && std::abs(sync240 - 32) <= 1 && std::abs(free240 - 16) <= 1
                        && std::abs((mLo + mHi) / 2 - 32.0) < 1.0 && std::abs(pHi - pLo - 8.0) < 0.5 && presetOff;
        std::printf("lfo 2: off bit-exact (max difference %g); against LFO 1 on pan at its settings, 6 waveforms: "
                    "triangle and sine within %.3f knob steps, %.1f%% of all blocks within 1 step, triangle %.1f..%.1f; with LFO 1 they add (%.1f..%.1f); Hold %.3f, One %.3f and Half %.3f still after; "
                    "half-cycles in 16 s synced at 120 / 240 BPM %d / %d (16 / 32), free at 240 %d (16); knob to 32: centre %.1f; Pitch depth +2: "
                    "+-%.2f st; cutoff x64: largest step %.2f a block; old preset off %d -> %s\n",
                    bypass, maxDiff, share * 100, lo2, hi2, loB, hiB, hHi - hLo, oHi - oLo, fHi - fLo, sync120, sync240, free240,
                    (mLo + mHi) / 2, (pHi - pLo) / 2, maxStep, presetOff, ok ? "PASS" : "FAIL");
    }

    // the Syntakt amp envelope (NOTES.md step 44): off is the old sound,
    // bit for bit; on, its gain is the ratio of a render with it to one
    // without, block by block (Tone, its own Decay infinite)
    {
        struct Ev { int block, note, vel; };
        auto run = [](std::initializer_list<std::pair<const char*, float>> settings, std::vector<Ev> events, int blocks,
                      bool smooth = true, Product product = Product::Full) {
            CyclesEmuProcessor f(product);
            cyclesemu::setPlain(f.state(), "midimode", 1);
            cyclesemu::setPlain(f.state(), "t1_machine", 4);
            cyclesemu::setPlain(f.state(), "t1_decay", 127);
            cyclesemu::setPlain(f.state(), "t1_lfodepth", 64);
            for (auto [id, v] : settings)
                cyclesemu::setPlain(f.state(), id, v);
            f.prepareToPlay(48000, 32);
            if (!smooth)
                f.setSmoothFilter(false);
            juce::AudioBuffer<float> buf(2, 32);
            std::vector<float> rms, raw;
            for (int b = 0; b < blocks; ++b)
            {
                juce::MidiBuffer midi;
                for (auto& e : events)
                    if (e.block == b)
                        midi.addEvent(e.vel > 0 ? juce::MidiMessage::noteOn(1, e.note, (juce::uint8)e.vel)
                                                : juce::MidiMessage::noteOff(1, e.note), 0);
                buf.clear();
                f.processBlock(buf, midi);
                double e2 = 0;
                for (int i = 0; i < 32; ++i)
                {
                    e2 += buf.getSample(0, i) * buf.getSample(0, i);
                    raw.push_back(buf.getSample(0, i));
                }
                rms.push_back((float)std::sqrt(e2 / 32));
            }
            return std::pair{rms, raw};
        };
        // the envelope's gain at block b: on / off, the same notes
        auto gain = [](const std::vector<float>& on, const std::vector<float>& off, int b) {
            return off[(size_t)b] > 1e-6f ? on[(size_t)b] / off[(size_t)b] : 0.0f;
        };
        CyclesEmuProcessor probe(Product::Full);
        const bool available = probe.ampAvailable();
        const int n0 = 10;   // the note's block; the attack starts at the block after
        const std::vector<Ev> held{{n0, 60, 127}, {n0 + 450, 60, 0}};   // a key up after 300 ms
        auto [ref, refRaw] = run({}, held, 1500);
        // off: changing its settings while off changes nothing
        auto [off, offRaw] = run({{"t1_aatk", 100}, {"t1_ahold", 10}, {"t1_adec", 5}, {"t1_amode", 1}}, held, 1500);
        float bypass = 0;
        for (size_t i = 0; i < refRaw.size(); ++i)
            bypass = std::max(bypass, std::abs(refRaw[i] - offRaw[i]));
        // AHD, fixed hold 32 (about 337 ms): the key up at 300 ms is ignored
        auto [hold, h2] = run({{"t1_amp", 1}, {"t1_amode", 0}, {"t1_aatk", 0}, {"t1_ahold", 32}, {"t1_adec", 8}}, held, 1500);
        // AHD, hold NOTE: it falls at the key up (decay 8: -60 dB in about 40 ms)
        auto [note, n2] = run({{"t1_amp", 1}, {"t1_amode", 0}, {"t1_aatk", 0}, {"t1_ahold", 127}, {"t1_adec", 8}}, held, 1500);
        // ADSR: sustain 64 (half), release 8 after the key up
        auto [adsr, a2] = run({{"t1_amp", 1}, {"t1_amode", 1}, {"t1_aatk", 0}, {"t1_adec", 8}, {"t1_asus", 64}, {"t1_arel", 8}},
                              held, 1500);
        const float holdIn = gain(hold, ref, n0 + 400), holdOut = gain(hold, ref, n0 + 640);
        const float noteIn = gain(note, ref, n0 + 400), noteOut = gain(note, ref, n0 + 540);
        const float sus = gain(adsr, ref, n0 + 300), rel = gain(adsr, ref, n0 + 540);
        // retrigger at 200 ms, attack 64 (about 262 ms): AENR on starts from 0, off carries on
        const std::vector<Ev> twice{{n0, 60, 127}, {n0 + 300, 60, 127}};
        auto [ref2, r2] = run({}, twice, 700);
        auto [en1, e1] = run({{"t1_amp", 1}, {"t1_aatk", 64}, {"t1_ahold", 127}, {"t1_aenr", 1}}, twice, 700);
        auto [en0, e0] = run({{"t1_amp", 1}, {"t1_aatk", 64}, {"t1_ahold", 127}, {"t1_aenr", 0}}, twice, 700);
        const float before = gain(en1, ref2, n0 + 298), reset1 = gain(en1, ref2, n0 + 302), reset0 = gain(en0, ref2, n0 + 302);
        // legato: a second key while the first is held is no new trigger
        const std::vector<Ev> leg{{n0, 60, 127}, {n0 + 300, 64, 127}};
        auto [lref, lr] = run({{"t1_legato", 1}}, leg, 700);
        auto [lon, lo] = run({{"t1_legato", 1}, {"t1_amp", 1}, {"t1_ahold", 127}}, leg, 700);
        const float legatoDip = gain(lon, lref, n0 + 302);
        // the smooth filter off: the same envelope
        auto [holdRaw, hr] = run({{"t1_amp", 1}, {"t1_ahold", 32}, {"t1_adec", 8}}, held, 900, false);
        auto [refRaw2, rr] = run({}, held, 900, false);
        const float smoothOff = gain(holdRaw, refRaw2, n0 + 400), smoothOff2 = gain(holdRaw, refRaw2, n0 + 640);
        // the sends follow it: a short envelope leaves almost no reverb tail
        const std::vector<Ev> hit{{n0, 60, 127}};
        auto [wetRef, w1] = run({{"t1_reverbsend", 127}, {"fx_reverbsize", 100}}, hit, 1500);
        auto [wetEnv, w2] = run({{"t1_reverbsend", 127}, {"fx_reverbsize", 100}, {"t1_amp", 1}, {"t1_ahold", 0}, {"t1_adec", 8}},
                                hit, 1500);
        double tailRef = 0, tailEnv = 0;
        for (int b = 600; b < 1500; ++b)
            tailRef += wetRef[(size_t)b] * wetRef[(size_t)b], tailEnv += wetEnv[(size_t)b] * wetEnv[(size_t)b];
        // a track preset saved before the envelope loads with it off
        bool presetOff = false;
        {
            CyclesEmuProcessor f(Product::Full);
            auto tree = cyclesemu::trackToTree(f.state(), 0);
            tree.removeProperty("amp", nullptr);
            cyclesemu::setPlain(f.state(), "t1_amp", 1);
            cyclesemu::treeToTrack(f.state(), 0, tree);
            presetOff = f.state().getRawParameterValue("t1_amp")->load() < 0.5f;
        }
        // CyclesVoice: 7 notes on 6 voices (one stolen), all released: every voice's release ends
        // (after 400 blocks: the sends, set to 0, ramp down from the kit's first)
        const int v0 = 400;
        std::vector<Ev> chord;
        for (int k = 0; k < 7; ++k)
            chord.push_back({v0 + k, 60 + 2 * k, 120}), chord.push_back({v0 + 300, 60 + 2 * k, 0});
        auto [voice, v2] = run({{"t1_amp", 1}, {"t1_amode", 1}, {"t1_asus", 100}, {"t1_arel", 8}, {"t1_delaysend", 0},
                                {"t1_reverbsend", 0}}, chord, v0 + 700, true, Product::Voice);
        const float voiceHeld = voice[(size_t)(v0 + 200)], voiceAfter = voice[(size_t)(v0 + 300 + 150)];
        const bool ok = available && bypass == 0.0f && holdIn > 0.97f && holdOut < 0.01f && noteIn > 0.97f && noteOut < 0.01f
                        && std::abs(sus - 0.5f) < 0.03f && rel < 0.01f && before > 0.6f && reset1 < 0.05f
                        && std::abs(reset0 - before) < 0.05f && legatoDip > 0.97f && std::abs(smoothOff - holdIn) < 0.01f
                        && smoothOff2 < 0.01f && tailEnv < tailRef * 0.05 && presetOff && voiceHeld > 0.01f
                        && voiceAfter < voiceHeld * 0.001f;
        std::printf("amp envelope: available %d; off bit-exact (max difference %g); AHD hold 32 ignores the key up "
                    "(gain %.3f at 273 ms, %.4f at 433 ms); hold NOTE follows it (%.3f, then %.4f); ADSR sustain 64 %.3f, "
                    "release %.4f; retrigger AENR on %.3f -> %.3f, off -> %.3f; legato %.3f; smooth filter off %.3f / %.4f; "
                    "reverb tail %.1f dB; old preset off %d; CyclesVoice %.4f -> %.6f -> %s\n",
                    available, bypass, holdIn, holdOut, noteIn, noteOut, sus, rel, before, reset1, reset0, legatoDip,
                    smoothOff, smoothOff2, 10 * std::log10(tailEnv / tailRef + 1e-30), presetOff, voiceHeld, voiceAfter,
                    ok ? "PASS" : "FAIL");
    }

    // a sample library for SP TWINSHOT: 100 short WAVs in subfolders; 64
    // are taken, the first by name or a random pick, the same for a seed
    {
        auto lib = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("cyem-twin-library");
        lib.deleteRecursively();
        juce::WavAudioFormat wav;
        for (int i = 0; i < 100; ++i)
        {
            auto f = lib.getChildFile(i < 50 ? "Kicks" : "Snares/More").getChildFile("hit " + juce::String(i) + ".wav");
            f.getParentDirectory().createDirectory();
            auto os = std::make_unique<juce::FileOutputStream>(f);
            std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(os.get(), 44100, 1, 16, {}, 0));
            os.release();
            juce::AudioBuffer<float> b(1, 441);
            for (int k = 0; k < 441; ++k)
                b.setSample(0, k, 0.5f * std::sin(0.05f * (float)(k * (i + 1))));
            w->writeFromAudioSampleBuffer(b, 0, 441);
        }
        lib.getChildFile(".hidden.wav").create();
        cyclesemu::TwinSamples first, r1, r1again, r2;
        const int n = first.loadFolder(lib);
        r1.loadFolder(lib, {true, 7});
        r1again.loadFolder(lib, {true, 7});
        r2.loadFolder(lib, {true, 8});
        auto names = [](const cyclesemu::TwinSamples& t) {
            juce::StringArray a;
            for (int i = 0; i < t.count(); ++i)
                a.add(t.name(i));
            return a.joinIntoString(",");
        };
        const bool ok = n == 64 && first.found() == 100 && first.name(0) == "hit 0" && first.name(1) == "hit 1"
                        && first.name(63) == "hit 63" && r1.count() == 64 && names(r1) == names(r1again)
                        && names(r1) != names(r2) && names(r1) != names(first);
        std::printf("twinshot library: %d of %d files (subfolders), first %s..%s; random picks 64, the same for a seed, "
                    "another for another -> %s\n", n, first.found(), first.name(0).toRawUTF8(), first.name(63).toRawUTF8(),
                    ok ? "PASS" : "FAIL");
        lib.deleteRecursively();
    }

    // the kit-saved fanfare: plays, finite, at -6 dBFS, about 2 s
    {
        auto b = cyclesemu::renderFanfare();
        bool finite = true;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < b.getNumSamples(); ++i)
                finite &= std::isfinite(b.getSample(c, i));
        const float peak = b.getMagnitude(0, b.getNumSamples());
        const double secs = b.getNumSamples() / 44100.0;
        if (auto* fp = std::fopen(out.getChildFile("fanfare.f32").getFullPathName().toRawUTF8(), "wb"))
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
                for (int c = 0; c < 2; ++c)
                {
                    const float x = b.getSample(c, i);
                    std::fwrite(&x, 4, 1, fp);
                }
            std::fclose(fp);
        }
        std::printf("fanfare: %.2f s, peak %.3f -> %s\n", secs, peak,
                    finite && std::abs(peak - 0.5f) < 0.01f && secs > 1.5 && secs < 3.0 ? "PASS" : "FAIL");
    }

    // the SOUND SETUP panel, with a slot of each set
    {
        e->showPage(0);
        cyclesemu::setPlain(proc.state(), "t1_vm1dst", 10);
        cyclesemu::setPlain(proc.state(), "t1_vm1dep", 40);
        cyclesemu::setPlain(proc.state(), "t1_km1dst", 2);
        cyclesemu::setPlain(proc.state(), "t1_km1dep", -16);
        cyclesemu::setPlain(proc.state(), "t1_fkey", 64);
        e->showSoundSetup(true);
        auto img = e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f);
        auto f = out.getChildFile("editor-sound-setup.png");
        f.deleteFile();
        juce::FileOutputStream os(f);
        juce::PNGImageFormat().writeImageToStream(img, os);
        e->showSoundSetup(false);
        for (auto* id : {"t1_vm1dst", "t1_vm1dep", "t1_km1dst", "t1_km1dep", "t1_fkey"})
            cyclesemu::setPlain(proc.state(), id, 0);
    }

    // the delay and reverb engines: each type's wet signal (a render with
    // the send up, less one without), its level against the firmware's own,
    // its CPU; and nothing non-finite
    {
        auto wetOf = [](int which, int type, double& seconds) {
            auto render = [&](int send) {
                CyclesEmuProcessor f(Product::Full);
                cyclesemu::setPlain(f.state(), "midimode", 1);
                cyclesemu::setPlain(f.state(), "t1_machine", 4);
                cyclesemu::setPlain(f.state(), "t1_decay", 40);
                cyclesemu::setPlain(f.state(), "t1_delaysend", which == 0 ? (float)send : 0.0f);
                cyclesemu::setPlain(f.state(), "t1_reverbsend", which == 1 ? (float)send : 0.0f);
                cyclesemu::setPlain(f.state(), which == 0 ? "fx_delaytype" : "fx_reverbtype", (float)type);
                cyclesemu::setPlain(f.state(), "fx_delayfeedback", 60);
                f.createFxEnginesNow();
                f.prepareToPlay(48000, 32);
                juce::AudioBuffer<float> buf(2, 32);
                std::vector<float> out;
                const auto t0 = juce::Time::getHighResolutionTicks();
                for (int b = 0; b < 1500 + 6000; ++b)
                {
                    juce::MidiBuffer midi;
                    if (b >= 1500 && (b - 1500) % 1500 == 0)
                        midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)110), 0);
                    buf.clear();
                    f.processBlock(buf, midi);
                    if (b >= 1500)
                        for (int i = 0; i < 32; ++i)
                            out.push_back(buf.getSample(0, i)), out.push_back(buf.getSample(1, i));
                }
                seconds = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - t0);
                return out;
            };
            const auto with = render(110), without = render(0);   // (the time: the last render, 5 s of audio)
            if (const char* dir = std::getenv("FX_WET_DIR"))   // the wet signal, raw float stereo, to listen or analyse
                if (auto* fp = std::fopen(juce::File(dir).getChildFile(juce::String(which) + "_" + juce::String(type) + ".f32")
                                              .getFullPathName().toRawUTF8(), "wb"))
                {
                    for (size_t i = 0; i < with.size(); ++i)
                    {
                        const float d = with[i] - without[i];
                        std::fwrite(&d, 4, 1, fp);
                    }
                    std::fclose(fp);
                }
            double e = 0;
            bool finite = true;
            for (size_t i = 0; i < with.size(); ++i)
            {
                const double d = (double)with[i] - without[i];
                finite &= std::isfinite(d);
                e += d * d;
            }
            return finite ? std::sqrt(e / (double)with.size()) : -1.0;
        };
        bool ok = true;
        for (int which = 0; which < 2; ++which)
        {
            double base = 0;
            for (int type = 0; type < (which ? cyclesemu::kReverbTypes : cyclesemu::kDelayTypes); ++type)
            {
                double secs = 0;
                const double wet = wetOf(which, type, secs);
                if (type == 0)
                    base = wet;
                ok &= wet > 0 && std::abs(20 * std::log10(wet / base)) < 6.0;
                std::printf("fx engines: %s %-9s wet %.4f (%+.1f dB against Cycles), render %.1f %% of real time\n",
                            which ? "reverb" : "delay", (which ? cyclesemu::kReverbTypeNames : cyclesemu::kDelayTypeNames)[type]
                                .toRawUTF8(), wet, wet > 0 ? 20 * std::log10(wet / base) : -99.0, secs / 5.0 * 100.0);
            }
        }
        std::printf("fx engines: every type plays, finite, within 6 dB of the Cycles' level -> %s\n", ok ? "PASS" : "FAIL");
    }

    // SP TWINSHOT's two pages on track 1
    {
        e->showPage(0);
        const float m0 = proc.state().getParameter("t1_machine")->getValue();
        cyclesemu::setPlain(proc.state(), "t1_machine", 11);
        for (int page = 0; page < 2; ++page)
        {
            e->setTwinPage2(page == 1);
            auto img = e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f);
            auto f = out.getChildFile(juce::String("editor-twinshot-p") + juce::String(page + 1) + ".png");
            f.deleteFile();
            juce::FileOutputStream os(f);
            juce::PNGImageFormat().writeImageToStream(img, os);
        }
        e->setTwinPage2(false);
        proc.state().getParameter("t1_machine")->setValueNotifyingHost(m0);
    }

    // the LFO 2 page: on the cutoff, a triangle, Trig
    {
        e->showPage(0);
        for (auto [id, v] : {std::pair<const char*, float>{"t1_l2dest", 11}, {"t1_l2depth", 80}, {"t1_l2reset", 1}})
            cyclesemu::setPlain(proc.state(), id, v);
        e->setLfo2View(true);
        auto img = e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f);
        auto f = out.getChildFile("editor-lfo2.png");
        f.deleteFile();
        juce::FileOutputStream os(f);
        juce::PNGImageFormat().writeImageToStream(img, os);
        e->setLfo2View(false);
        for (auto [id, v] : {std::pair<const char*, float>{"t1_l2dest", 0}, {"t1_l2depth", 64}, {"t1_l2reset", 0}})
            cyclesemu::setPlain(proc.state(), id, v);
    }

    // the AMP page, the envelope on (ADSR)
    {
        e->showPage(0);
        cyclesemu::setPlain(proc.state(), "t1_amp", 1);
        cyclesemu::setPlain(proc.state(), "t1_amode", 1);
        e->setAmpView(true);
        auto img = e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f);
        auto f = out.getChildFile("editor-amp.png");
        f.deleteFile();
        juce::FileOutputStream os(f);
        juce::PNGImageFormat().writeImageToStream(img, os);
        e->setAmpView(false);
        cyclesemu::setPlain(proc.state(), "t1_amp", 0);
        cyclesemu::setPlain(proc.state(), "t1_amode", 0);
        // picking a machine sets MODE as the Syntakt does: SY machines ADSR, drums AHD
        ParamControl* machine = nullptr;
        std::function<void(juce::Component&)> find = [&](juce::Component& c) {
            if (c.getProperties()["helpId"].toString() == "t1_machine")
                if (auto* pc = dynamic_cast<ParamControl*>(&c))
                    machine = pc;
            for (auto* ch : c.getChildren())
                find(*ch);
        };
        find(*e);
        const float m0 = proc.state().getParameter("t1_machine")->getValue();
        std::vector<int> modes;
        for (int m : {4, 0, 9, 1, 11})   // Tone, Kick, SY Bits, Snare, Twinshot
        {
            if (machine && machine->onPicked)
                machine->onPicked(m);
            modes.push_back(juce::roundToInt(proc.state().getRawParameterValue("t1_amode")->load()));
        }
        proc.state().getParameter("t1_machine")->setValueNotifyingHost(m0);
        cyclesemu::setPlain(proc.state(), "t1_amode", 0);
        std::printf("amp mode on picking a machine: Tone %d, Kick %d, SY Bits %d, Snare %d, Twinshot %d -> %s\n", modes[0],
                    modes[1], modes[2], modes[3], modes[4],
                    modes == std::vector<int>{1, 0, 1, 0, 0} ? "PASS" : "FAIL");
    }

    if (std::getenv("MACHINE_DIAG"))
        for (int m = 0; m < 12; ++m)
            for (int loaded = 1; loaded >= (m == 11 ? 0 : 1); --loaded)
            {
                CyclesEmuProcessor f(Product::Full);
                if (!loaded)
                    f.loadTwinFolder(juce::File("/nonexistent"));
                cyclesemu::setPlain(f.state(), "midimode", 1);
                cyclesemu::setPlain(f.state(), "t1_machine", (float)m);
                f.prepareToPlay(48000, 32);
                juce::AudioBuffer<float> buf(2, 32);
                double e = 0, zc = 0;
                float prev = 0;
                for (int b = 0; b < 1500 + 400; ++b)
                {
                    juce::MidiBuffer midi;
                    if (b == 1500)
                        midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)110), 0);
                    buf.clear();
                    f.processBlock(buf, midi);
                    if (b >= 1500)
                        for (int i = 0; i < 32; ++i)
                        {
                            const float x = buf.getSample(0, i);
                            e += (double)x * x;
                            zc += (prev < 0) != (x < 0);
                            prev = x;
                        }
                }
                std::printf("machine %2d (%s)%s: rms %.4f, zero crossings %.0f, engine machine %d, twin samples %d\n", m,
                            cyclesemu::kMachineNames[m].toRawUTF8(), m == 11 ? (loaded ? " loaded" : " no samples") : "",
                            std::sqrt(e / (400 * 32)), zc, f.engineValue(0, 0x26), f.twinSamples().count());
            }

    // every control's help: each one with a help id has a text, on every
    // machine (the knobs' meanings change with it)
    {
        std::set<juce::String> ids;
        std::function<void(juce::Component&)> walk = [&](juce::Component& c) {
            if (c.getProperties().contains("helpId"))
                ids.insert(c.getProperties()["helpId"].toString());
            for (auto* k : c.getChildren())
                walk(*k);
        };
        walk(*e);
        int missing = 0;
        for (auto& id : ids)
            for (int m = 0; m < 12; ++m)
                if (cyclesemu::helpFor(id, m).title.isEmpty() || cyclesemu::helpFor(id, m).body.isEmpty())
                {
                    if (missing++ < 10)
                        std::printf("help: none for %s on machine %d\n", id.toRawUTF8(), m);
                }
        for (auto* id : {"t1_color", "t1_shape", "t1_sweep", "t1_contour", "t1_pitch", "t1_decay"})
            std::printf("help on SP Twinshot, %s: %s: %s\n", id, cyclesemu::helpFor(id, 11).title.toRawUTF8(),
                        cyclesemu::helpFor(id, 11).body.toRawUTF8());
        std::printf("help: %d controls, each with a text on all 12 machines -> %s\n", (int)ids.size(),
                    missing == 0 ? "PASS" : "FAIL");
    }

    // the help mode, as if hovering over a few controls
    const char* help[][3] = {{"0", "t1_color", "help-color"}, {"0", "t1_lforeset", "help-trigmode"}, {"1", "t2_color", "help-sybits-color"},
                             {"9", "fx_reverbsize", "help-reverbsize"}, {"0", "midimode", "help-midimode"}};
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
