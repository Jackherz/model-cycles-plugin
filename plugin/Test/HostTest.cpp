// Host the built CyclesEmu VST3 like a DAW and check it.
//
//  1. identity: at 48 kHz, notes on all six tracks at sample 0 and nothing
//     else; the plugin's output must equal the engine's own (aot/engine.c,
//     same notes, same state) sample for sample.
//  2. load: notes on every track every 0.5 s and a CC sweep, 128-sample
//     callbacks at 48 and 44.1 kHz; time each processBlock, write a WAV.
//
//   CyclesEmuHostTest [path/to/CyclesEmu.vst3] [out_dir]
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <algorithm>
#include <chrono>
#include "engine.h"

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

int identity(juce::AudioPluginFormatManager& fm, const juce::String& path, const juce::String& state)
{
    const int block = 128, blocks = 375;   // 1 s
    auto p = load(fm, path, 48000, block);
    if (!p) return 1;
    p->prepareToPlay(48000, block);
    int err = 0;
    mc_engine* g = mc_engine_new(state.toRawUTF8(), &err);
    if (!g) { std::printf("engine state: error %d\n", err); return 1; }
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
                midi.addEvent(juce::MidiMessage::noteOn(t + 1, 48 + 3 * t, (juce::uint8)110), 0);
                mc_engine_note(g, t, 48 + 3 * t, 110, 1, 0x0681);
            }
        buf.clear();
        p->processBlock(buf, midi);
        for (int k = 0; k < block / 32; ++k)
        {
            mc_engine_render(g, l, r);
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
    std::printf("identity at 48 kHz: %ld frames, %ld samples differ from the engine, peak %.3f -> %s\n",
                n, diffs, peak, diffs == 0 && peak > 0.01 ? "PASS" : "FAIL");
    return diffs == 0 && peak > 0.01 ? 0 : 1;
}

int load_test(juce::AudioPluginFormatManager& fm, const juce::String& path, double rate,
              const juce::File& wavOut)
{
    const int block = 128;
    const int total = (int)(rate * 10);
    auto p = load(fm, path, rate, block);
    if (!p) return 1;
    p->prepareToPlay(rate, block);
    juce::AudioBuffer<float> out(2, total), buf(2, block);
    std::vector<double> us;
    int pos = 0, nextNote = 0, ccv = 0;
    while (pos + block <= total)
    {
        juce::MidiBuffer midi;
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
} // namespace

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
    int rc = identity(fm, path, state);
    rc |= load_test(fm, path, 48000, outDir.getChildFile("hosttest-48k.wav"));
    rc |= load_test(fm, path, 44100, outDir.getChildFile("hosttest-44k1.wav"));
    return rc;
}
