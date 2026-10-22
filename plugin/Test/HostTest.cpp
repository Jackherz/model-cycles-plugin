// Host the built CyclesEmu VST3 like a DAW and check it.
//
//  1. identity: at 48 kHz, notes on all six tracks at sample 0 and nothing
//     else; the plugin's output must equal the engine's own (aot/engine.c,
//     same notes, same state) sample for sample.
//  2. load: notes on every track every 0.5 s and a CC sweep, 128-sample
//     callbacks at 48 and 44.1 kHz; time each processBlock, write a WAV.
//
//   CyclesEmuHostTest [path/to/CyclesEmu.vst3] [out_dir]
#include "../Source/Resampler.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <algorithm>
#include <chrono>
#include "engine.h"
#include "../Source/DcBlock.h"

namespace
{
std::unique_ptr<juce::AudioPluginInstance> load(juce::AudioPluginFormatManager& fm, const juce::String& path,
                                                double rate, int block)
{
    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* f : fm.getFormats())
        f->findAllTypesForFile(found, path);
    if (found.isEmpty())
        return nullptr;
    juce::String err;
    auto p = fm.createPluginInstance(*found[0], rate, block, err);
    if (p == nullptr)
        std::printf("load failed: %s\n", err.toRawUTF8());
    return p;
}

void writeWav(const juce::File& f, const juce::AudioBuffer<float>& buf, double rate)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w(
        wav.createWriterFor(new juce::FileOutputStream(f), rate, 2, 24, {}, 0));
    if (w)
        w->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples());
}

void setParam(juce::AudioPluginInstance& p, const juce::String& name, float normalised)
{
    for (auto* prm : p.getParameters())
        if (prm->getName(64) == name)
        {
            prm->setValue(normalised);
            return;
        }
    std::printf("no parameter %s\n", name.toRawUTF8());
}

// drum: notes C1-F1 on channel 1 in Drum Map mode; else channels 1-6 in Ch 1-6 mode.
int identity(juce::AudioPluginFormatManager& fm, const juce::String& path, const juce::String& state, bool drum)
{
    const int block = 128, blocks = 375;   // 1 s
    auto p = load(fm, path, 48000, block);
    if (!p) return 1;
    setParam(*p, "MIDI Mode", drum ? 0.0f : 1.0f);
    p->prepareToPlay(48000, block);
    DcBlock dl, dr;
    int err = 0;
    mc_engine* g = mc_engine_new(state.toRawUTF8(), &err);
    if (!g) { std::printf("engine state: error %d\n", err); return 1; }
    mc_engine_set_tempo(g, 120.0);   // as the plugin does with no host tempo
    if (mc_engine_get_param(g, 6, 0x16) > 126)   // and reverb size 127 as 126
        mc_engine_param(g, 6, 0x16, 126);
    juce::AudioBuffer<float> buf(2, block);
    float l[32], r[32];
    long diffs = 0, n = 0;
    double peak = 0;
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        if (b == 0)
            for (int t = 0; t < 6; ++t)
            {
                if (drum)
                {
                    midi.addEvent(juce::MidiMessage::noteOn(1, 36 + t, (juce::uint8)110), 0);
                    mc_engine_note(g, t, 60, 110, 1, 0x0681);
                }
                else
                {
                    midi.addEvent(juce::MidiMessage::noteOn(t + 1, 48 + 3 * t, (juce::uint8)110), 0);
                    mc_engine_note(g, t, 48 + 3 * t, 110, 1, 0x0681);
                }
            }
        buf.clear();
        p->processBlock(buf, midi);
        for (int k = 0; k < block / 32; ++k)
        {
            mc_engine_render(g, l, r);
            dl.process(l, 32);
            dr.process(r, 32);
            for (int i = 0; i < 32; ++i, ++n)
            {
                float a = buf.getSample(0, 32 * k + i), c = buf.getSample(1, 32 * k + i);
                diffs += (a != l[i]) + (c != r[i]);
                peak = std::max(peak, (double)std::abs(a));
            }
        }
    }
    mc_engine_free(g);
    p->releaseResources();
    std::printf("identity at 48 kHz (%s): %ld frames, %ld samples differ from the engine, peak %.3f -> %s\n",
                drum ? "Drum Map, C1-F1 on ch 1" : "Ch 1-6 = Tracks", n, diffs, peak,
                diffs == 0 && peak > 0.01 ? "PASS" : "FAIL");
    return diffs == 0 && peak > 0.01 ? 0 : 1;
}

int load_test(juce::AudioPluginFormatManager& fm, const juce::String& path, double rate,
              const juce::File& wavOut)
{
    const int block = 128;
    const int total = (int)(rate * 10);
    auto p = load(fm, path, rate, block);
    if (!p) return 1;
    setParam(*p, "MIDI Mode", 1.0f);
    p->prepareToPlay(rate, block);
    juce::AudioBuffer<float> out(2, total), buf(2, block);
    std::vector<double> us;
    int pos = 0, nextNote = 0, ccv = 0;
    while (pos + block <= total)
    {
        juce::MidiBuffer midi;
        if (pos == 0)   // every track through its filter (step 17 builds): a resonant low-pass
            for (int t = 0; t < 6; ++t)
            {
                midi.addEvent(juce::MidiMessage::controllerEvent(t + 1, 74, 36 + 4 * t), 0);
                midi.addEvent(juce::MidiMessage::controllerEvent(t + 1, 71, 90), 0);
            }
        if (pos >= nextNote)
        {
            for (int t = 0; t < 6; ++t)
                midi.addEvent(juce::MidiMessage::noteOn(t + 1, 40 + 5 * t, (juce::uint8)100), 7 * t);
            nextNote += (int)(rate / 2);
        }
        if ((pos / block) % 8 == 0)   // a decay sweep on track 6
        {
            ccv = (ccv + 3) % 128;
            midi.addEvent(juce::MidiMessage::controllerEvent(6, 80, ccv), 0);
        }
        buf.clear();
        auto t0 = std::chrono::steady_clock::now();
        p->processBlock(buf, midi);
        us.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count());
        for (int c = 0; c < 2; ++c)
            out.copyFrom(c, pos, buf, c, 0, block);
        pos += block;
    }
    p->releaseResources();
    writeWav(wavOut, out, rate);
    std::vector<int> slow;
    for (size_t i = 0; i < us.size(); ++i)
        if (us[i] > 2000) slow.push_back((int)i);
    if (!slow.empty())
    {
        std::printf("  callbacks over 2 ms:");
        for (int i : slow) std::printf(" #%d (%.0f us)", i, us[(size_t)i]);
        std::printf("\n");
    }
    std::sort(us.begin(), us.end());
    double mean = 0;
    for (double u : us) mean += u;
    mean /= (double)us.size();
    double budget = 1e6 * block / rate;
    std::printf("%.1f kHz, %d-sample callbacks: mean %.1f us, p50 %.1f, p99 %.1f, max %.1f "
                "(callback %.0f us: mean %.1f%%); peak %.3f, rms %.3f -> %s\n",
                rate / 1000, block, mean, us[us.size() / 2], us[us.size() * 99 / 100], us.back(), budget,
                100 * mean / budget, out.getMagnitude(0, pos), out.getRMSLevel(0, 0, pos),
                wavOut.getFullPathName().toRawUTF8());
    return 0;
}
// Two minutes with no MIDI: the output's mean (the firmware drifts it) and
// its level after the DC blocker.
int idle(juce::AudioPluginFormatManager& fm, const juce::String& path)
{
    const int block = 512;
    auto p = load(fm, path, 48000, block);
    if (!p) return 1;
    p->prepareToPlay(48000, block);
    juce::AudioBuffer<float> buf(2, block);
    double sum = 0, sum2 = 0;
    long n = 0;
    for (int b = 0; b < 48000 * 120 / block; ++b)
    {
        juce::MidiBuffer midi;
        buf.clear();
        p->processBlock(buf, midi);
        if (b >= 48000 * 60 / block)   // the second minute
            for (int i = 0; i < block; ++i, ++n)
            {
                double v = buf.getSample(0, i);
                sum += v;
                sum2 += v * v;
            }
    }
    double mean = sum / (double)n, rms = std::sqrt(sum2 / (double)n);
    std::printf("idle, second minute: mean %+.6f, rms %.6f (%.1f dBFS) -> %s\n", mean, rms,
                20 * std::log10(rms + 1e-12), std::abs(mean) < 1e-4 ? "PASS" : "FAIL");
    return std::abs(mean) < 1e-4 ? 0 : 1;
}
struct FixedTempo : juce::AudioPlayHead
{
    double bpm;
    explicit FixedTempo(double b) : bpm(b) {}
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm(bpm);
        return p;
    }
};

// One kick into the delay (delay time 64 = two beats) with the host at a
// given tempo; -> the first echo's time in beats (the delay's added signal).
double echoBeats(juce::AudioPluginFormatManager& fm, const juce::String& path, double bpm)
{
    double onset[2] = {0, 0};
    for (int send = 0; send < 2; ++send)
    {
        auto p = load(fm, path, 48000, 128);
        if (!p) return -1;
        FixedTempo head(bpm);
        p->setPlayHead(&head);
        setParam(*p, "MIDI Mode", 0.0f);
        setParam(*p, "T1 Machine", 0.0f);
        setParam(*p, "T1 Decay", 10.0f / 127);
        setParam(*p, "T1 Delay Send", send ? 1.0f : 0.0f);
        setParam(*p, "T1 Reverb Send", 0.0f);
        setParam(*p, "Delay Time", 64.0f / 127);
        setParam(*p, "Delay Feedback", 60.0f / 127);
        for (int t = 2; t <= 6; ++t)
            setParam(*p, "T" + juce::String(t) + " Mute", 1.0f);
        p->prepareToPlay(48000, 128);
        juce::AudioBuffer<float> buf(2, 128);
        std::vector<float> out;
        for (int b = 0; b < 375 * 4; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 375)
                midi.addEvent(juce::MidiMessage::noteOn(1, 36, (juce::uint8)127), 0);
            buf.clear();
            p->processBlock(buf, midi);
            if (b >= 375)
                out.insert(out.end(), buf.getReadPointer(0), buf.getReadPointer(0) + 128);
        }
        static std::vector<float> dry;
        if (send == 0)
            dry = out;
        else
        {
            float mx = 0;
            for (size_t i = 0; i < out.size(); ++i) mx = std::max(mx, std::abs(out[i] - dry[i]));
            for (size_t i = 0; i < out.size(); ++i)
                if (std::abs(out[i] - dry[i]) > 0.05f * mx) { onset[1] = (double)i / 48000; break; }
        }
    }
    return onset[1] * bpm / 60.0;
}

int tempo(juce::AudioPluginFormatManager& fm, const juce::String& path)
{
    double a = echoBeats(fm, path, 120), b = echoBeats(fm, path, 60);
    bool ok = std::abs(a - b) < 0.02 && std::abs(a - 2.0) < 0.1;
    std::printf("tempo: delay time 64 echoes after %.3f beats at 120 BPM, %.3f at 60 BPM -> %s\n",
                a, b, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
} // namespace

// The resampled path: one chord at sample 0 (so the engine renders the same
// stream at any host rate), at 48 kHz and at 44.1 kHz. The 44.1 kHz output
// must match the 48 kHz one put through the same kernel (Kaiser, Resampler's
// taps, beta and cutoff) evaluated continuously in double precision at the
// exact times the resampler uses: this checks the plugin's streaming
// (buffering, positions, event timing) and the table's precision.
int resampled(juce::AudioPluginFormatManager& fm, const juce::String& path)
{
    auto render = [&](double rate, int frames) {
        auto p = load(fm, path, rate, 128);
        juce::AudioBuffer<float> out(2, frames);
        if (!p) return out;
        setParam(*p, "MIDI Mode", 1.0f);
        p->prepareToPlay(rate, 128);
        juce::AudioBuffer<float> buf(2, 128);
        for (int pos = 0; pos + 128 <= frames; pos += 128)
        {
            juce::MidiBuffer midi;
            if (pos == 0)
                for (int t = 0; t < 6; ++t)
                    midi.addEvent(juce::MidiMessage::noteOn(t + 1, 40 + 5 * t, (juce::uint8)100), 0);
            buf.clear();
            p->processBlock(buf, midi);
            for (int c = 0; c < 2; ++c)
                out.copyFrom(c, pos, buf, c, 0, 128);
        }
        return out;
    };
    auto a = render(48000, 48000 * 3), b = render(44100, 44100 * 3);
    const double step = 48000.0 / 44100.0, D = Resampler::latency();
    const double fc = 0.5 * 44100.0 / 48000.0 - 0.0225;   // as Resampler::prepare
    const int T = Resampler::kTaps;
    auto i0 = [](double x) {
        double sum = 1, term = 1;
        for (int k = 1; k < 40; ++k)
            term *= (x / (2.0 * k)) * (x / (2.0 * k)), sum += term;
        return sum;
    };
    const double beta = 12.5, i0b = i0(beta);
    double e = 0, s = 0;
    for (int k = 4000; k < 44100 * 3 - 4000; ++k)
    {
        const double t = k * step + D, start = std::floor(k * step);
        double ref = 0, norm = 0;
        for (int j = 0; j < T; ++j)
        {
            const double x = start + j - t, r = x / (T / 2);
            const double win = std::abs(r) < 1 ? i0(beta * std::sqrt(1 - r * r)) / i0b : 0;
            const double h = (x == 0 ? 2 * fc : std::sin(2 * M_PI * fc * x) / (M_PI * x)) * win;
            ref += a.getSample(0, (int)start + j) * h;
            norm += h;
        }
        ref /= norm;
        const double d = b.getSample(0, k) - ref;
        e += d * d;
        s += ref * ref;
    }
    const double db = 10 * std::log10(e / s);
    std::printf("resampled path at 44.1 kHz against an independent reference: residual %.1f dB -> %s\n", db,
                db < -100 ? "PASS" : "FAIL");
    return db < -100 ? 0 : 1;
}

// CyclesVoice: one sound on six voices. On the Tone machine, a chord at 6
// voices must be louder than one note (three voices sound), and the same
// chord at 1 voice must sound like a single note (each cuts the last).
// Then the load: a note every 1/8 s across six pitches, 10 s.
int voiceTest(juce::AudioPluginFormatManager& fm, const juce::String& path)
{
    auto render = [&](std::vector<int> notes, int voices, double* usMean) {
        auto p = load(fm, path, 48000, 128);
        if (!p)
            return -1.0;
        setParam(*p, "Machine", 4.0f / 10.0f);                 // Tone
        setParam(*p, "Decay", 110.0f / 127.0f);
        setParam(*p, "Voices", (float)(voices - 1) / 5.0f);
        p->prepareToPlay(48000, 128);
        juce::AudioBuffer<float> buf(2, 128);
        double sum = 0, us = 0;
        // settings, then a second for the firmware to settle them, then the notes
        const int at = 375;
        int n = 0, blocks = usMean ? 48000 * 10 / 128 : at + 300;
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            if (usMean == nullptr && b == at)
                for (int k : notes)
                    midi.addEvent(juce::MidiMessage::noteOn(1, k, (juce::uint8)110), 0);
            if (usMean != nullptr && b % 47 == 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, notes[(size_t)(b / 47) % notes.size()], (juce::uint8)100), 0);
            buf.clear();
            auto t0 = std::chrono::steady_clock::now();
            p->processBlock(buf, midi);
            us += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
            if (usMean != nullptr || b >= at)
                for (int i = 0; i < 128; ++i, ++n)
                    sum += (double)buf.getSample(0, i) * buf.getSample(0, i);
        }
        if (usMean)
            *usMean = us / blocks;
        return std::sqrt(sum / juce::jmax(1, n));
    };
    const double one = render({60}, 6, nullptr), chord = render({60, 64, 67}, 6, nullptr),
                 mono = render({60, 64, 67}, 1, nullptr);
    // three independent voices add their energies; one voice leaves one note
    const double e64 = render({64}, 6, nullptr), e67 = render({67}, 6, nullptr);
    const double sum = std::sqrt(one * one + e64 * e64 + e67 * e67);
    double us = 0;
    render({48, 55, 60, 64, 67, 72}, 6, &us);
    const bool ok = one > 0 && std::abs(chord / sum - 1.0) < 0.05 && std::abs(mono / one - 1.0) < 0.3;
    std::printf("voice: one note %.4f, chord at 6 voices %.4f (its notes' energies %.4f), chord at 1 voice %.4f; "
                "busy load %.1f us per 128-sample callback (%.1f%%) -> %s\n",
                one, chord, sum, mono, us, 100 * us / 2666.7, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::String path = argc > 1 ? juce::String(argv[1])
        : juce::File::getSpecialLocation(juce::File::userHomeDirectory)
              .getChildFile("Library/Audio/Plug-Ins/VST3/CyclesEmu.vst3").getFullPathName();
    juce::File outDir = argc > 2 ? juce::File(argv[2]) : juce::File::getCurrentWorkingDirectory();
    juce::String state = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Application Support/CyclesEmu/state").getFullPathName();
    juce::AudioPluginFormatManager fm;
    fm.addDefaultFormats();
    int rc = identity(fm, path, state, false);
    rc |= identity(fm, path, state, true);
    rc |= idle(fm, path);
    rc |= tempo(fm, path);
    rc |= resampled(fm, path);
    rc |= voiceTest(fm, path.replace("CyclesEmu.vst3", "CyclesVoice.vst3"));
    rc |= load_test(fm, path, 48000, outDir.getChildFile("hosttest-48k.wav"));
    rc |= load_test(fm, path, 44100, outDir.getChildFile("hosttest-44k1.wav"));
    return rc;
}
