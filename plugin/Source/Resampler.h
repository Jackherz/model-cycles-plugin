// Polyphase windowed-sinc resampler for the engine's 48 kHz stream.
//
// 192 taps, Kaiser window (beta 12.5, about -120 dB stopband), cutoff set by the
// lower of the two rates so that downsampling (48 -> 44.1 kHz) does not
// alias. The kernel is tabulated at 1024 fractional phases, and each output
// interpolates linearly between two neighbouring phases. Each phase is
// normalised to unity gain at DC.
//
// Usage: the caller keeps the input in one contiguous buffer. process()
// reads src[0 .. needed(n)) and returns how many leading samples it is done
// with; the caller drops those before the next call.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

class Resampler
{
public:
    static constexpr int kTaps = 192, kPhases = 1024;

    void prepare(double inRate, double outRate)
    {
        step = inRate / outRate;
        pos = 0.0;
        // -6 dB point: the lower Nyquist less half the transition band
        // (Kaiser, 120 dB, 192 taps: about 0.041 of the input rate)
        const double nyq = 0.5 * std::min(1.0, outRate / inRate);
        const double fc = nyq - 0.0225;
        const double beta = 12.5, i0b = bessel0(beta);
        table.assign((size_t)(kPhases + 1) * kTaps, 0.0f);
        for (int p = 0; p <= kPhases; ++p)
        {
            const double frac = (double)p / kPhases;
            double sum = 0.0;
            std::vector<double> row(kTaps);
            for (int k = 0; k < kTaps; ++k)
            {
                // tap k sits at input time k; the output at D + frac
                const double x = (double)k - (kTaps / 2 - 1) - frac;
                const double r = x / (kTaps / 2);
                const double w = std::abs(r) < 1.0 ? bessel0(beta * std::sqrt(1.0 - r * r)) / i0b : 0.0;
                const double s = x == 0.0 ? 2.0 * fc : std::sin(2.0 * M_PI * fc * x) / (M_PI * x);
                row[(size_t)k] = s * w;
                sum += row[(size_t)k];
            }
            for (int k = 0; k < kTaps; ++k)
                table[(size_t)p * kTaps + (size_t)k] = (float)(row[(size_t)k] / sum);
        }
    }

    void reset() { pos = 0.0; }

    // Input samples process(n) will read.
    int needed(int n) const { return (int)std::floor(pos + step * (n - 1)) + kTaps + 1; }

    // Writes n outputs from src (at least needed(n) samples); returns how
    // many leading input samples are no longer needed.
    int process(const float* src, float* out, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const int i0 = (int)pos;
            const float a = (float)(pos - i0) * kPhases;
            const int p = std::min(kPhases - 1, (int)a);
            const float f = a - (float)p;
            const float y0 = dot(table.data() + (size_t)p * kTaps, src + i0);
            const float y1 = dot(table.data() + (size_t)(p + 1) * kTaps, src + i0);
            out[i] = y0 + f * (y1 - y0);
            pos += step;
        }
        const int used = (int)pos;
        pos -= used;
        return used;
    }

    // Both channels of a stereo stream at once (one phase computation).
    int process(const float* srcL, const float* srcR, float* outL, float* outR, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const int i0 = (int)pos;
            const float a = (float)(pos - i0) * kPhases;
            const int p = std::min(kPhases - 1, (int)a);
            const float f = a - (float)p;
            const float* h0 = table.data() + (size_t)p * kTaps;
            const float* h1 = h0 + kTaps;
            const float l0 = dot(h0, srcL + i0), l1 = dot(h1, srcL + i0);
            const float r0 = dot(h0, srcR + i0), r1 = dot(h1, srcR + i0);
            outL[i] = l0 + f * (l1 - l0);
            outR[i] = r0 + f * (r1 - r0);
            pos += step;
        }
        const int used = (int)pos;
        pos -= used;
        return used;
    }

    // Delay through the filter, in input samples.
    static constexpr double latency() { return kTaps / 2 - 1; }

private:
    static float dot(const float* h, const float* x)
    {
        // four independent vector accumulators, so the reduction pipelines
        typedef float v4 __attribute__((vector_size(16), aligned(4)));
        v4 s0 = {}, s1 = {}, s2 = {}, s3 = {};
        for (int k = 0; k < kTaps; k += 16)
        {
            s0 += *(const v4*)(h + k) * *(const v4*)(x + k);
            s1 += *(const v4*)(h + k + 4) * *(const v4*)(x + k + 4);
            s2 += *(const v4*)(h + k + 8) * *(const v4*)(x + k + 8);
            s3 += *(const v4*)(h + k + 12) * *(const v4*)(x + k + 12);
        }
        v4 s = (s0 + s1) + (s2 + s3);
        return (s[0] + s[1]) + (s[2] + s[3]);
    }

    static double bessel0(double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 40; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
        }
        return sum;
    }

    double step = 1.0, pos = 0.0;
    std::vector<float> table;
};
