// Compare the plugin's resampler with JUCE's WindowedSincInterpolator (what
// the plugin used before): sines through 48 -> 44.1 kHz and 48 -> 96 kHz,
// error against an ideal sine (least-squares fit at the output), aliasing
// of a tone above the output's Nyquist, and time per 128 outputs.
//   CyclesEmuResamplerTest
#include <juce_audio_basics/juce_audio_basics.h>
#include "../Source/Resampler.h"
#include <chrono>
#include <cstdio>

namespace
{
std::vector<float> sine(double f, double rate, int n, double amp = 0.5)
{
    std::vector<float> x((size_t)n);
    for (int i = 0; i < n; ++i)
        x[(size_t)i] = (float)(amp * std::sin(2.0 * M_PI * f * i / rate));
    return x;
}

// Fit a*sin + b*cos at f over y[skip..n-skip): returns {level of the fit
// in dBFS, residual relative to the fit in dB}.
std::pair<double, double> fit(const std::vector<float>& y, double f, double rate, int skip)
{
    double ss = 0, sc = 0, cc = 0, ys = 0, yc = 0;
    for (size_t i = (size_t)skip; i < y.size() - (size_t)skip; ++i)
    {
        double s = std::sin(2 * M_PI * f * (double)i / rate), c = std::cos(2 * M_PI * f * (double)i / rate);
        ss += s * s; sc += s * c; cc += c * c; ys += y[i] * s; yc += y[i] * c;
    }
    double det = ss * cc - sc * sc, a = (ys * cc - yc * sc) / det, b = (yc * ss - ys * sc) / det;
    double e = 0, p = 0;
    for (size_t i = (size_t)skip; i < y.size() - (size_t)skip; ++i)
    {
        double m = a * std::sin(2 * M_PI * f * (double)i / rate) + b * std::cos(2 * M_PI * f * (double)i / rate);
        e += (y[i] - m) * (y[i] - m);
        p += m * m;
    }
    return {10 * std::log10(p / (double)(y.size() - 2 * (size_t)skip) * 2 + 1e-30),
            10 * std::log10(e / (p + 1e-30) + 1e-30)};
}

double rmsDb(const std::vector<float>& y, int skip)
{
    double e = 0;
    for (size_t i = (size_t)skip; i < y.size() - (size_t)skip; ++i)
        e += (double)y[i] * y[i];
    return 10 * std::log10(e / (double)(y.size() - 2 * (size_t)skip) * 2 + 1e-30);
}

std::vector<float> viaJuce(const std::vector<float>& x, double in, double out, double* usPer128)
{
    juce::WindowedSincInterpolator r;
    int n = (int)((double)x.size() * out / in) - 400;
    std::vector<float> y((size_t)n);
    auto t0 = std::chrono::steady_clock::now();
    int used = 0;
    for (int i = 0; i + 128 <= n; i += 128)
        used += r.process(in / out, x.data() + used, y.data() + i, 128);
    *usPer128 = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / (n / 128);
    y.resize((size_t)(n / 128 * 128));
    return y;
}

std::vector<float> viaOurs(const std::vector<float>& x, double in, double out, double* usPer128)
{
    Resampler r;
    r.prepare(in, out);
    int n = (int)((double)x.size() * out / in) - 400;
    std::vector<float> y((size_t)n);
    auto t0 = std::chrono::steady_clock::now();
    int used = 0;
    for (int i = 0; i + 128 <= n; i += 128)
        used += r.process(x.data() + used, y.data() + i, 128);
    *usPer128 = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / (n / 128);
    y.resize((size_t)(n / 128 * 128));
    return y;
}
} // namespace

int main()
{
    const double in = 48000;
    for (double out : {44100.0, 96000.0})
    {
        std::printf("48 kHz -> %.1f kHz\n", out / 1000);
        for (double f : {1000.0, 10000.0, 18000.0, 20000.0})
        {
            auto x = sine(f, in, 96000);
            double tj, to;
            auto j = fit(viaJuce(x, in, out, &tj), f, out, 400);
            auto o = fit(viaOurs(x, in, out, &to), f, out, 400);
            std::printf("  %5.0f Hz at -6 dBFS: JUCE %+6.2f dBFS, error %6.1f dB | ours %+6.2f dBFS, error %6.1f dB\n",
                        f, j.first, j.second, o.first, o.second);
        }
        if (out < in)
            for (double f : {22500.0, 23500.0})
            {
                auto x = sine(f, in, 96000);
                double tj, to;
                std::printf("  %5.0f Hz (above the output's Nyquist): alias JUCE %6.1f dBFS | ours %6.1f dBFS\n", f,
                            rmsDb(viaJuce(x, in, out, &tj), 400), rmsDb(viaOurs(x, in, out, &to), 400));
            }
        auto x = sine(1000, in, 48000 * 20);
        double tj, to;
        viaJuce(x, in, out, &tj);
        viaOurs(x, in, out, &to);
        std::printf("  time per 128 outputs, one channel: JUCE %.1f us | ours %.1f us\n", tj, to);
    }
    return 0;
}
