/* CHOMPI TEMPO's dual delay and reverb: see fxwrap.h. The port's source is
 * included unchanged (but for its reverb's float memory: ../README.md);
 * its knob mappings (set_main, set_feedback, set_mix), its per-sample engine
 * (process48) and its loader are used as they are.
 * Only the clock is CyclesEmu's: TEMPO's 24 pulses a beat run free at the
 * engine's tempo, at 48 kHz (the port's run_clock assumes Move's 44.1). */
#define move_audio_fx_init_v2 cyem_munchi_init_v2
#include "../munchi-delay/munchi_delay.cpp"
#undef move_audio_fx_init_v2
#include "fxwrap.h"

extern "C" {

void *cyem_chompi_create(void)
{
    void *mem = calloc(1, sizeof(MunchiDelay));   // TEMPO's classes expect zeroed RAM
    if (!mem)
        return nullptr;
    MunchiDelay *d = new (mem) MunchiDelay();
    for (int k = 0; k < K_COUNT; k++)
        d->p[k] = kDefs[k].def;
    d->p[K_MIX] = 1.f;   // wet only: the dry is the track's own
    loader_main(d);      // the buffers, here (not on the audio thread)
    for (int k = 0; k < K_COUNT; k++)
        apply(d, k);
    d->applied = true;
    return d;
}

void cyem_chompi_destroy(void *p)
{
    MunchiDelay *d = (MunchiDelay *)p;
    if (!d)
        return;
    free(d->granular);
    free(d->frozen);
    d->~MunchiDelay();
    free(d);
}

void cyem_chompi_set(void *p, float wand, float random, float feedback)
{
    MunchiDelay *d = (MunchiDelay *)p;
    if (!d)
        return;
    if (wand != d->p[K_WAND])
        set_value(d, K_WAND, wand);
    if (random != d->p[K_RANDOM])
        set_value(d, K_RANDOM, random);
    if (feedback != d->p[K_FEEDBACK])
        set_value(d, K_FEEDBACK, feedback);
}

void cyem_chompi_process(void *p, const float *in, float *out, int frames, double bpm)
{
    MunchiDelay *d = (MunchiDelay *)p;
    if (!d || !d->ready.load(std::memory_order_acquire)) {
        memset(out, 0, sizeof(float) * 2 * frames);
        return;
    }
    if (!(bpm > 0.0))
        bpm = 120.0;
    if (bpm < 40.0)   // the 10 s buffer holds the 2-bar interval from 40 BPM up
        bpm = 40.0;
    d->clock.setBpm((float)bpm);
    d->pulse_phase += bpm * 24.0 / 60.0 * frames / 48000.0;
    while (d->pulse_phase >= 1.0) {
        d->pulse_phase -= 1.0;
        clock_pulse(d);
    }
    for (int i = 0; i < frames; i++)
        process48(d, in[2 * i], in[2 * i + 1], &out[2 * i], &out[2 * i + 1]);
}

struct TempoVerb
{
    daisysp::Reverb reverb;
};

void *cyem_tempoverb_create(void)
{
    void *mem = calloc(1, sizeof(TempoVerb));
    if (!mem)
        return nullptr;
    TempoVerb *v = new (mem) TempoVerb();
    v->reverb.Init(48000.f);
    v->reverb.SetAmount(1.f);       // wet only
    v->reverb.SetInputGain(.3f);    // as TEMPO
    v->reverb.SetTime(.7f);
    v->reverb.SetLowpass(.7f);
    v->reverb.SetDiffusion(.625f);
    return v;
}

void cyem_tempoverb_destroy(void *p)
{
    TempoVerb *v = (TempoVerb *)p;
    if (!v)
        return;
    v->~TempoVerb();
    free(v);
}

void cyem_tempoverb_set(void *p, float time, float lowpass, float diffusion)
{
    TempoVerb *v = (TempoVerb *)p;
    v->reverb.SetTime(time);
    v->reverb.SetLowpass(lowpass);
    v->reverb.SetDiffusion(diffusion);
}

void cyem_tempoverb_process(void *p, const float *in, float *out, int frames)
{
    TempoVerb *v = (TempoVerb *)p;
    for (int i = 0; i < frames; i++) {
        float l = in[2 * i], r = in[2 * i + 1];
        v->reverb.Process(&l, &r);
        out[2 * i] = l;
        out[2 * i + 1] = r;
    }
}

}   // extern "C"
