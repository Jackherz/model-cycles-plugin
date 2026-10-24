#include "Fanfare.h"
#include "Settings.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <cmath>
#include <memory>

namespace cyclesemu
{
namespace
{
constexpr double kRate = 44100.0;

struct Note
{
    int midi;    // 0: a rest
    double ms;
};

double hz(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }

// a step's smoothing (polyBLEP): the pulses without the aliasing's fizz
double blep(double t, double dt)
{
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.0;
    }
    if (t > 1.0 - dt)
    {
        t = (t - 1.0) / dt;
        return t * t + t + t + 1.0;
    }
    return 0.0;
}

// one voice's notes into the buffer: pulse (duty > 0) or triangle (duty 0)
void voice(std::vector<double>& out, const std::vector<Note>& notes, double duty, double gain, bool vibratoLast)
{
    double at = 0.0, phase = 0.0, tri = 0.0;
    for (size_t k = 0; k < notes.size(); ++k)
    {
        const auto& n = notes[k];
        const size_t start = (size_t)(at * kRate / 1000.0), len = (size_t)(n.ms * kRate / 1000.0);
        at += n.ms;
        if (n.midi == 0)
            continue;
        const bool last = k + 1 == notes.size();
        const size_t release = (size_t)(kRate * (last ? 0.45 : 0.03));
        for (size_t i = 0; i < len + release && start + i < out.size(); ++i)
        {
            const double t = i / kRate;
            double f = hz(n.midi);
            if (last && vibratoLast && t > 0.18)   // the held note sings
                f *= std::pow(2.0, 0.22 / 12.0 * std::sin(2 * juce::MathConstants<double>::pi * 5.5 * (t - 0.18))
                                       * juce::jmin(1.0, (t - 0.18) / 0.25));
            const double dt = f / kRate;
            phase += dt;
            if (phase >= 1.0)
                phase -= 1.0;
            double s;
            if (duty > 0.0)
            {
                s = (phase < duty ? 1.0 : -1.0) + blep(phase, dt) - blep(std::fmod(phase - duty + 1.0, 1.0), dt);
            }
            else   // triangle: the integrated square, leaky
            {
                const double sq = (phase < 0.5 ? 1.0 : -1.0) + blep(phase, dt) - blep(std::fmod(phase + 0.5, 1.0), dt);
                tri = 0.999 * tri + 4.0 * dt * sq;
                s = tri;
            }
            // a brassy swell: quick attack, a little decay, the release after the note
            double env = juce::jmin(1.0, t / 0.004) * (0.75 + 0.25 * std::exp(-t / 0.06));
            if (i >= len)
                env *= std::exp(-(double)(i - len) / (double)release * 5.0);
            else if (last)
                env *= 1.0 - 0.3 * juce::jmin(1.0, t / 0.9);
            out[start + i] += s * env * gain;
        }
    }
}

// noise hits: (time in ms, length in ms, level)
void drums(std::vector<double>& out, std::initializer_list<std::array<double, 3>> hits)
{
    juce::Random rng(1995);
    for (const auto& h : hits)
    {
        const size_t start = (size_t)(h[0] * kRate / 1000.0), len = (size_t)(h[1] * kRate / 1000.0);
        double lp = 0.0, prev = 0.0;
        for (size_t i = 0; i < len * 4 && start + i < out.size(); ++i)
        {
            const double w = rng.nextDouble() * 2.0 - 1.0;
            lp += 0.55 * (w - lp);
            const double hp = lp - prev;   // a bright, short hiss
            prev = lp;
            out[start + i] += hp * h[2] * std::exp(-(double)i / (double)len * 2.5);
        }
    }
}
} // namespace

juce::AudioBuffer<float> renderFanfare()
{
    // G G G | C . G C | E . D E | G (held): over C, then G, then C again
    const std::vector<Note> lead = {{67, 95}, {67, 95}, {67, 95}, {72, 285}, {67, 95}, {72, 95},
                                    {76, 285}, {74, 95}, {76, 95}, {79, 950}};
    const std::vector<Note> harmony = {{64, 95}, {64, 95}, {64, 95}, {64, 285}, {64, 95}, {67, 95},
                                       {72, 285}, {71, 95}, {72, 95}, {76, 950}};
    const std::vector<Note> bass = {{0, 285}, {48, 475}, {43, 475}, {48, 950}};
    const size_t total = (size_t)(kRate * 2.4);
    std::vector<double> mono(total, 0.0), side(total, 0.0);
    voice(mono, lead, 0.25, 0.22, true);
    voice(side, harmony, 0.5, 0.13, true);
    voice(mono, bass, 0.0, 0.35, false);
    drums(mono, {{{0, 25, 0.10}}, {{95, 25, 0.10}}, {{190, 25, 0.12}}, {{285, 70, 0.30}}, {{760, 70, 0.30}},
                 {{1235, 380, 0.34}}});
    juce::AudioBuffer<float> b(2, (int)total);
    double peak = 1e-9;
    for (size_t i = 0; i < total; ++i)
        peak = juce::jmax(peak, std::abs(mono[i]) + std::abs(side[i]));
    const double norm = 0.5 / peak;   // -6 dBFS
    for (size_t i = 0; i < total; ++i)   // the harmony a little wide
    {
        b.setSample(0, (int)i, (float)((mono[i] + side[i] * 1.0) * norm));
        b.setSample(1, (int)i, (float)((mono[i] + side[i] * 0.6) * norm));
    }
    return b;
}

bool fanfareOn() { return settings().getBoolValue("fanfare", true); }

void setFanfareOn(bool on)
{
    settings().setValue("fanfare", on);
    settings().saveIfNeeded();
}

void playFanfare()
{
    if (!fanfareOn())
        return;
    auto wav = settings().getFile().getSiblingFile("fanfare-v1.wav");
    if (!wav.existsAsFile())
    {
        auto b = renderFanfare();
        wav.getParentDirectory().createDirectory();
        juce::WavAudioFormat format;
        auto os = std::make_unique<juce::FileOutputStream>(wav);
        if (!os->openedOk())
            return;
        std::unique_ptr<juce::AudioFormatWriter> w(format.createWriterFor(os.get(), kRate, 2, 16, {}, 0));
        if (w == nullptr)
            return;
        os.release();   // the writer owns it now
        w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
    }
    static std::unique_ptr<juce::ChildProcess> player;   // one at a time: a new save restarts it
    if (player != nullptr)
        player->kill();
    player = std::make_unique<juce::ChildProcess>();
    player->start(juce::StringArray{"/usr/bin/afplay", wav.getFullPathName()}, 0);
}
} // namespace cyclesemu
