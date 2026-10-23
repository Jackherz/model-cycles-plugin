// The delay's and reverb's engines: see FxEngines.h and NOTES.md step 37.
// Each engine's knobs, from the FX page's four and a third each:
//   delay  CHOMPI  TIME -> the Wand (down: a clean echo, 1 bar .. 1/16 of the
//                  tempo; 64 off; up: melting into reverb), FEEDBACK, RANDOM
//          Tape    TIME (a note length, as the Cycles' delay), FEEDBACK, TONE
//   reverb CloudSeed  SIZE -> decay, TONE -> high cut, SPACE -> room size
//          CHOMPI     SIZE -> time, TONE -> low-pass, DIFFUSION
//          PSX        SIZE -> decay, TONE -> the model (Room, Studio S/M/L,
//                     Hall, Space Echo), LEVEL
#include "FxEngines.h"
#include "fxwrap.h"
#include <cmath>
#include <cstring>

namespace cyclesemu
{
const juce::StringArray kDelayTypeNames{"Cycles", "CHOMPI", "Tape"};
const juce::StringArray kReverbTypeNames{"Cycles", "CloudSeed", "CHOMPI", "PSX"};

namespace
{
// each engine's output trim, so a type change keeps the level: the wet
// level against the firmware's own at the same settings (EditorShot "fx
// engines": CHOMPI -12.1 dB, Tape -8.4, CloudSeed +9.2, CHOMPI's reverb
// +5.7, PSX -1.6 untrimmed)
const float kTrim[2][4] = {{1.0f, 4.03f, 2.63f, 0.0f}, {1.0f, 0.347f, 0.519f, 1.20f}};

const cyem_fx* v2For(int which, int type)
{
    if (which == 0)
        return type == 2 ? cyem_fx_spacecho() : nullptr;
    return type == 1 ? cyem_fx_cloudseed() : type == 3 ? cyem_fx_psxverb() : nullptr;
}

void setf(const cyem_fx* fx, void* e, const char* key, double v)
{
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.4f", v);
    fx->set(e, key, buf);
}
} // namespace

FxEngines::~FxEngines()
{
    for (int which = 0; which < 2; ++which)
        for (int type = 1; type < 4; ++type)
            if (void* e = engines[which][type].load())
            {
                if (auto* fx = v2For(which, type))
                    fx->destroy(e);
                else if (which == 0)
                    cyem_chompi_destroy(e);
                else
                    cyem_tempoverb_destroy(e);
            }
}

bool FxEngines::ready(int which, int type) const
{
    return type == 0 || engines[which][type].load(std::memory_order_acquire) != nullptr;
}

void FxEngines::ensure(int which, int type)
{
    if (type <= 0 || type >= (which ? kReverbTypes : kDelayTypes) || engines[which][type].load() != nullptr)
        return;
    void* e = nullptr;
    if (auto* fx = v2For(which, type))
    {
        e = fx->create();
        if (e != nullptr)
            fx->set(e, "mix", "1");   // wet only: the dry is the tracks' own
    }
    else
        e = which == 0 ? cyem_chompi_create() : cyem_tempoverb_create();
    engines[which][type].store(e, std::memory_order_release);
}

void FxEngines::run(int which, int type, const FxKnobs& k, const float* in, const float* fw, float* out)
{
    void* e = type > 0 ? engines[which][type].load(std::memory_order_acquire) : nullptr;
    if (e == nullptr)   // the firmware's
    {
        std::memcpy(out, fw, sizeof(float) * 64);
        return;
    }
    // the knobs, when they change
    FxKnobs& was = applied[which][type];
    const bool first = !appliedOnce[which][type];
    appliedOnce[which][type] = true;
    if (which == 0 && type == 1)   // CHOMPI
    {
        cyem_chompi_set(e, juce::jlimit(-1.0f, 1.0f, (k.time - 64) / 63.0f), k.delayx / 127.0f, k.feedback / 127.0f);
        cyem_chompi_process(e, in, out, 32, k.bpm);
    }
    else if (which == 1 && type == 2)   // CHOMPI's reverb
    {
        cyem_tempoverb_set(e, 0.3f + 0.68f * k.size / 127.0f, 0.2f + 0.8f * k.tone / 127.0f, k.reverbx / 127.0f);
        cyem_tempoverb_process(e, in, out, 32);
    }
    else
    {
        const cyem_fx* fx = v2For(which, type);
        if (which == 0)   // Tape: TIME a note length (32 = a beat) at the tempo
        {
            if (first || k.time != was.time || k.bpm != was.bpm)
            {
                const int ms = juce::jlimit(20, 2000, juce::roundToInt(juce::jmax(1, k.time) / 32.0 * 60000.0 / k.bpm));
                fx->set(e, "time", juce::String(ms).toRawUTF8());
            }
            if (first || k.feedback != was.feedback)
                setf(fx, e, "feedback", k.feedback / 127.0);
            if (first || k.delayx != was.delayx)
                setf(fx, e, "tone", k.delayx / 127.0);
        }
        else if (type == 1)   // CloudSeed
        {
            if (first || k.size != was.size)
                setf(fx, e, "decay", k.size / 127.0);
            if (first || k.tone != was.tone)
                setf(fx, e, "high_cut", k.tone / 127.0);
            if (first || k.reverbx != was.reverbx)
                setf(fx, e, "size", k.reverbx / 127.0);
        }
        else   // PSX
        {
            if (first || k.size != was.size)
                setf(fx, e, "decay", k.size / 127.0);
            if (first || k.tone != was.tone)
                fx->set(e, "model", juce::String(juce::jlimit(0, 5, k.tone * 6 / 128)).toRawUTF8());
            if (first || k.reverbx != was.reverbx)
                setf(fx, e, "reverb_level", k.reverbx / 127.0);
        }
        int16_t io[64];
        for (int i = 0; i < 64; ++i)
            io[i] = (int16_t)juce::jlimit(-32768, 32767, (int)std::lrint(in[i] * 32768.0f));
        fx->process(e, io, 32);
        for (int i = 0; i < 64; ++i)
            out[i] = io[i] * (1.0f / 32768.0f);
    }
    was = k;
    const float trim = kTrim[which][type];
    if (trim != 1.0f)
        for (int i = 0; i < 64; ++i)
            out[i] *= trim;
}

void FxEngines::render(int which, int type, const FxKnobs& k, const float* in, const float* fw, float* out)
{
    if (!ready(which, type))
        type = shown[which];   // not created yet: stay where it was
    if (type != shown[which])
    {
        from[which] = shown[which];
        shown[which] = type;
        fade[which] = 0.0f;
    }
    run(which, type, k, in, fw, out);
    if (fade[which] < 1.0f)
    {
        float old[64];
        run(which, from[which], k, in, fw, old);
        const float f0 = fade[which], f1 = juce::jmin(1.0f, f0 + 1.0f / 64.0f);
        for (int i = 0; i < 64; ++i)
        {
            const float f = f0 + (f1 - f0) * (i / 2) / 32.0f;
            out[i] = out[i] * f + old[i] * (1.0f - f);
        }
        fade[which] = f1;
    }
}
} // namespace cyclesemu
