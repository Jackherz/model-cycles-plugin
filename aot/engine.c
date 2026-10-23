/*
 * The Model:Cycles engine on the AOT render: what a plugin calls.
 *
 * State comes from a dump of a stopped Model:Cycles (aot/dump_state.py):
 * every mapped region and the CPU context the render is entered with. Each
 * block writes what the audio interrupt and the hardware would have
 * provided (NOTES.md "Per block"), runs FUN_40058c5e, and reads the ring
 * half it rendered. Notes and parameters are the firmware's own engine
 * calls (NOTES.md "The interface"), run as compiled code between blocks.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "runtime.h"
#include "engine.h"

#define RENDER_ISR 0x40058c5eu
#define FRAME_PC 0x40058a08u
#define ISR_WORDS 0x40a78e04u
#define SCRATCH 0x30000000u
#define CALL_SP (SCRATCH + 0x7000)
#define RENDER_SP (SCRATCH + 0x8000)
#define REQUEST (SCRATCH + 0x1000)
#define KIT_PTR 0x800017e4u
#define RING 0x4a3ed080u

#define F_SETPARAM 0x40059534u
#define F_SETLEVEL 0x40059594u
#define F_TRIG 0x4005894au

#define F_SETMUTE 0x400594f0u
#define MUTE_STATE 0x40a700c0u           /* the state block the firmware passes */

struct mc_engine {
    mc_ctx *x;
    mc_cpu tmpl;
    uint32_t counter, kit;
    int phase;
    uint64_t blocks;
    /* CyclesFX: the next block's input and how much of it each send gets */
    const float *in_l, *in_r;
    float in_delay, in_reverb;
    /* the smooth track filter (below), when on */
    int smooth_filter;
    struct tf_state {
        double ic1, ic2;             /* the SVF's integrators */
        double g, k, w;              /* coefficients and wet share where the last block ended */
        double env;                  /* the envelope, 1 at a note */
        int16_t c, r, a, d;          /* this block's smoothed words */
        int on, lp, trig;
    } tf[6];
};

/* The mixer (0x400567ba) sums the tracks' delay sends into an interleaved
 * stereo bus of fractional Q31 (L 0x8000b990, R 0x8000b994, 8 bytes a
 * frame, 32 frames), runs the delay on it (0x40057488 at 0x4005699c), sums
 * the reverb sends (with the delay's return) into the same bus and runs the
 * reverb (0x400579c4 at 0x40056a2c). The render built with --hook at the
 * two calls' first pushes lets the host add to the bus just before each. */
#define FX_HOOK_DELAY  0x4005698cu
#define FX_HOOK_REVERB 0x40056a1cu
#define SEND_BUS       0x8000b990u

static void fx_hook(mc_ctx *x, uint32_t pc)
{
    mc_engine *g = x->hook_user;
    const float amount = pc == FX_HOOK_DELAY ? g->in_delay : pc == FX_HOOK_REVERB ? g->in_reverb : 0.0f;
    if (!g->in_l || amount == 0.0f)
        return;
    for (int i = 0; i < 32; i++)
        for (int c = 0; c < 2; c++) {
            const uint32_t a = SEND_BUS + 8 * i + 4 * c;
            const double add = (double)(c ? g->in_r[i] : g->in_l[i]) * amount * 2147483648.0;
            double s = (double)(int32_t)mc_ld32(x, a) + add;
            s = s > 2147483647.0 ? 2147483647.0 : s < -2147483648.0 ? -2147483648.0 : s;
            mc_st32_raw(x, a, (uint32_t)(int32_t)s);
        }
}

/* The track filter (Modded-Cycles' track-filter, NOTES.md step 17) works
 * a block at a time: its coefficients change every 32 frames, so a fast LFO
 * on the cutoff steps audibly (zipper clicks), and a cutoff swept across the
 * centre flips the filter between low- and high-pass. With the smooth filter
 * on, a hook before its loop over the tracks (in tf_post, after it stores
 * the block's trigs) shows it every cutoff at the centre, where it leaves
 * the track alone, and one after the loop puts the cutoffs back and runs the
 * same filter here: the same laws (gen_track_filter.py), its coefficients
 * ramped sample by sample from one block's to the next, the side (LP or HP)
 * the knob's, with modulation opening or closing it within that side, and,
 * with the knob at OFF, a change of side made through the dry signal. */
#define TF_HOOK_BEFORE 0x4014ab9cu
#define TF_HOOK_AFTER  0x4014abe2u
#define TF_TRIG        0x40160ee4u
#define TRACK_BUFS     0x80001858u
#define SMOOTHED       0x8000100cu       /* + 14 + 66 t + 2 k */
#define SMOOTH_STATE   0x80001204u       /* + 132 t + 4 k: the smoothers' 32-bit state */

static double tf_cutoff_hz(double i) { double f = 5.0 * pow(4400.0, i / 16384.0); return f < 20000.0 ? f : 20000.0; }

static void tf_before(mc_engine *g)
{
    const uint32_t trig = mc_ld32(g->x, TF_TRIG);
    for (int t = 0; t < 6; t++) {
        struct tf_state *f = &g->tf[t];
        const uint32_t w = SMOOTHED + 14 + 66 * t + 2 * 28;
        f->c = (int16_t)mc_ld16(g->x, w);
        f->r = (int16_t)mc_ld16(g->x, w + 2);
        f->a = (int16_t)mc_ld16(g->x, w + 4);
        f->d = (int16_t)mc_ld16(g->x, w + 6);
        f->trig = (trig >> t) & 1;
        mc_st16_raw(g->x, w, 0x4000);
    }
}

static void tf_after(mc_engine *g)
{
    for (int t = 0; t < 6; t++) {
        struct tf_state *f = &g->tf[t];
        const uint32_t w = SMOOTHED + 14 + 66 * t + 2 * 28;
        mc_st16_raw(g->x, w, (uint16_t)f->c);
        /* the envelope, as the firmware's: restarted by a note, else decaying */
        if (f->trig)
            f->env = 1.0;
        else if (f->env > 0.0) {
            const double tau = 0.005 * pow(2000.0, (f->d < 0 ? 0 : f->d) / 32512.0);
            f->env *= exp(-1.0 / (1500.0 * tau));
            if (f->env < 0x10000 / 2147483648.0)
                f->env = 0.0;
        }
        const int knob = (int16_t)mc_ld16(g->x, g->kit + 0x1c + 100 * t + 0x4c) - 0x4000, now = f->c - 0x4000;
        if (knob == 0 && now == 0 && (!f->on || f->w == 0.0)) {
            f->on = 0;                   /* OFF: the track as it is */
            continue;
        }
        int lp = knob ? knob < 0 : now ? now < 0 : f->lp;
        double wt = knob ? 1.0 : fmin(1.0, abs(now) / 256.0);
        if (f->on && lp != f->lp) {
            lp = f->lp;                  /* finish on the old side, through the dry signal */
            wt = 0.0;
        }
        double idx = (lp ? f->c : now) + (f->a - 0x4000) * f->env;
        idx = idx < 0 ? 0 : idx > 0x3fff ? 0x3fff : idx;
        const double fc = tf_cutoff_hz(idx);
        const double scale = (1 - pow(fc / 24000.0, 2.5)) * fmin(fmax((fc - 10.0) / 20.0, 0.0), 1.0);
        const double gt = tan(3.14159265358979323846 * fc / 48000.0);
        const double kt = 1.0 / (0.5 * pow(100.0, (f->r < 0 ? 0 : f->r) * scale / 32512.0));
        uint8_t *buf = g->x->mem + TRACK_BUFS + 128 * t;
        const double norm = 1.0 / 2147483648.0;
        if (!f->on) {                    /* from rest: LP holds the input's level, HP starts at 0 */
            f->on = 1;
            f->ic1 = 0.0;
            f->ic2 = lp ? (int32_t)mc_ld32m(buf, 0) * norm : 0.0;
            f->g = gt, f->k = kt, f->w = wt;
        }
        f->lp = lp;
        const double dg = (gt - f->g) / 32, dk = (kt - f->k) / 32, dw = (wt - f->w) / 32;
        double gg = f->g, kk = f->k, ww = f->w, ic1 = f->ic1, ic2 = f->ic2;
        for (int i = 0; i < 32; i++) {
            gg += dg, kk += dk, ww += dw;
            const double x = (int32_t)mc_ld32m(buf, 4 * i) * norm;
            const double a1 = 1.0 / (1.0 + gg * (gg + kk)), a2 = gg * a1, a3 = gg * a2;
            const double v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2 * v1 - ic1;
            ic2 = 2 * v2 - ic2;
            const double y = lp ? v2 : x - kk * v1 - v2;
            double o = (x + ww * (y - x)) * 2147483648.0;
            o = o > 2147483647.0 ? 2147483647.0 : o < -2147483648.0 ? -2147483648.0 : o;
            mc_st32m(buf, 4 * i, (uint32_t)(int32_t)o);
        }
        if (fabs(ic1) < 1e-20) ic1 = 0.0;    /* no denormals */
        if (fabs(ic2) < 1e-20) ic2 = 0.0;
        f->g = gt, f->k = kt, f->w = wt, f->ic1 = ic1, f->ic2 = ic2;
    }
}

static void engine_hook(mc_ctx *x, uint32_t pc)
{
    mc_engine *g = x->hook_user;
    if (pc == TF_HOOK_BEFORE || pc == TF_HOOK_AFTER) {
        if (g->smooth_filter)
            (pc == TF_HOOK_BEFORE ? tf_before : tf_after)(g);
        return;
    }
    fx_hook(x, pc);
}

static void engine_update_hook(mc_engine *g)
{
    g->x->hook = g->in_l || g->smooth_filter ? engine_hook : NULL;
    g->x->hook_user = g;
}

void mc_engine_snap_param(mc_engine *g, int track, int offset)
{
    /* the firmware's smoother set to a sound word itself, so a change made
     * at a trigger lands at once instead of sliding: its 32-bit state
     * (SMOOTH_STATE + 132 t + 4 k, the word in the top half, found by
     * watching SRAM follow a stepped pitch) and the 16-bit copy the voices
     * read (SMOOTHED + 14 + 66 t + 2 k), k = (offset - 0x14) / 2 */
    if (track < 0 || track > 5 || offset < 0x14 || offset > 0x52 || (offset & 1))
        return;
    const int k = (offset - 0x14) / 2;
    const uint32_t word = mc_ld16(g->x, g->kit + 0x1c + 100 * track + offset);
    mc_st32_raw(g->x, SMOOTH_STATE + 132 * track + 4 * k, word << 16);
    mc_st16_raw(g->x, SMOOTHED + 14 + 66 * track + 2 * k, word);
}

void mc_engine_set_smooth_filter(mc_engine *g, int on)
{
    g->smooth_filter = on;
    memset(g->tf, 0, sizeof g->tf);
    engine_update_hook(g);
}

void mc_engine_set_input(mc_engine *g, const float *l, const float *r, float delaySend, float reverbSend)
{
    g->in_l = l;
    g->in_r = r;
    g->in_delay = delaySend;
    g->in_reverb = reverbSend;
    engine_update_hook(g);
}

mc_engine *mc_engine_new(const char *dir, int *err)
{
    char path[1024];
    int e = 0;
    mc_engine *g = calloc(1, sizeof *g);
    FILE *f = NULL;
    if (!g || !(g->x = mc_ctx_new())) { e = -1; goto fail; }
    snprintf(path, sizeof path, "%s/regions.bin", dir);
    if (!(f = fopen(path, "rb"))) { e = -2; goto fail; }
    uint32_t hdr[2];
    while (fread(hdr, 4, 2, f) == 2) {          /* address, length, bytes */
        if (fread(g->x->mem + hdr[0], 1, hdr[1], f) != hdr[1]) { e = -3; goto fail; }
    }
    fclose(f);
    snprintf(path, sizeof path, "%s/cpu.bin", dir);
    if (!(f = fopen(path, "rb")) || fread(&g->tmpl, sizeof g->tmpl, 1, f) != 1 ||
        fread(&g->counter, 4, 1, f) != 1 || fread(&g->phase, 4, 1, f) != 1) { e = -4; goto fail; }
    fclose(f);
    g->kit = mc_ld32(g->x, KIT_PTR);
    memset(g->x->mem + SCRATCH, 0, 0x10000);         /* fault the scratch pages in now */
    /* Start with silent effects. The state was saved while the factory
     * pattern played, and its kit's reverb size is 127, at which the reverb
     * never decays: the old tail would hum forever (NOTES.md step 9). The
     * reverb's state is 0x4a300000-0x4a3ecfff (the output ring follows at
     * 0x4a3ed080); the delay lines are the 4 MB eDMA ring at 0x4a400000. */
    memset(g->x->mem + 0x4a300000u, 0, 0x4a3ed000u - 0x4a300000u);
    memset(g->x->mem + 0x4a400000u, 0, 0x400000u);
    if (err) *err = 0;
    return g;
fail:
    if (f) fclose(f);
    mc_engine_free(g);
    if (err) *err = e;
    return NULL;
}

void mc_engine_free(mc_engine *g)
{
    if (!g) return;
    mc_ctx_free(g->x);
    free(g);
}

static int call(mc_engine *g, uint32_t func, const uint32_t *args, int nargs)
{
    mc_ctx *x = g->x;
    mc_st32_raw(x, CALL_SP, SCRATCH);                /* return address: stop there */
    for (int i = 0; i < nargs; i++)
        mc_st32_raw(x, CALL_SP + 4 + 4 * i, args[i]);
    mc_cpu cpu = g->tmpl;
    cpu.a[7] = CALL_SP;
    return mc_run(x, &cpu, func, SCRATCH);
}

int mc_engine_param(mc_engine *g, int track, int offset, int value)
{
    return mc_engine_param_word(g, track, offset, value << 8);
}

int mc_engine_param_word(mc_engine *g, int track, int offset, int word)
{
    mc_ctx *x = g->x;
    if (track < 0 || track > 6)
        return -1;
    uint32_t v = (uint32_t)word & 0xffff;
    if (track < 6) {                                 /* sound record word */
        mc_st16_raw(x, g->kit + 0x1c + 100 * track + offset, v);
        uint32_t a[3] = {v, (uint32_t)track, (uint32_t)(offset - 0x14) / 2};
        return call(g, F_SETPARAM, a, 3);
    }
    mc_st16_raw(x, g->kit + 0x274 + offset, v);            /* FX block */
    uint32_t a[3] = {v, 6, (uint32_t)(offset - 0x12) / 2 + 9};
    return call(g, F_SETPARAM, a, 3);
}

int mc_engine_level(mc_engine *g, int track, int value)
{
    uint32_t v = (uint32_t)value << 8;
    mc_st16_raw(g->x, g->kit + 0x10 + 2 * track, v);
    uint32_t a[2] = {(uint32_t)track, v};
    return call(g, F_SETLEVEL, a, 2);
}

int mc_engine_mute(mc_engine *g, int track, int muted)
{
    uint32_t m = mc_ld32(g->x, MUTE_STATE + 0x14);
    m = muted ? (m | (1u << track)) : (m & ~(1u << track));
    mc_st32_raw(g->x, MUTE_STATE + 0x14, m);
    uint32_t a[1] = {MUTE_STATE};
    return call(g, F_SETMUTE, a, 1);
}

int mc_engine_note(mc_engine *g, int track, int note, int velocity, int on, uint32_t trig_flags)
{
    uint32_t w[14] = {0};
    w[0] = (uint32_t)track;
    w[1] = (uint32_t)note;
    if (on) {
        w[2] = (uint32_t)velocity; w[3] = 1; w[4] = 0x30001u | trig_flags;
        w[5] = 0xffffffffu; w[9] = 0xffffffffu; w[10] = 0x7f;
    } else {
        w[3] = 2; w[5] = 0xffffffffu;
    }
    for (int i = 0; i < 14; i++)
        mc_st32_raw(g->x, REQUEST + 4 * i, w[i]);
    uint32_t a[1] = {REQUEST};
    return call(g, F_TRIG, a, 1);
}

int mc_engine_render(mc_engine *g, float *left, float *right)
{
    mc_ctx *x = g->x;
    uint32_t half = (g->blocks + (uint64_t)g->phase) & 1 ? 0x4a3ed180u : 0x4a3ed080u;   /* DMA plays */
    mc_st32_raw(x, ISR_WORDS, 0);
    mc_st32_raw(x, ISR_WORDS + 4, g->counter + (uint32_t)g->blocks);
    mc_st32_raw(x, ISR_WORDS + 8, 1);
    mc_st32_raw(x, RENDER_SP, 0x42fc2608u);
    mc_st32_raw(x, RENDER_SP + 4, FRAME_PC);
    mc_st32_raw(x, 0xfc04c010u, 0x80000000u);
    mc_st32_raw(x, 0xfc045640u, half);
    mc_st32_raw(x, 0xfc050014u, 0);
    /* Read only by builds with Modded-Cycles' Syntakt engines (NOTES.md
     * step 16): eDMA channel 30's CSR (DONE, as the eDMA leaves it), INTC0's
     * force register, and DMA timer 0's counter. The timer is the load
     * governor's clock: held still, every block measures zero load, so the
     * governor never fades voices to save time the hardware does not have. */
    mc_st16_raw(x, 0xfc0453deu, 0x0080);
    mc_st32_raw(x, 0xfc048010u, 0);
    mc_st32_raw(x, 0xfc07000cu, 0);
    mc_cpu cpu = g->tmpl;
    cpu.a[7] = RENDER_SP;
    x->mmio_count = 0;
    int st = mc_run(x, &cpu, RENDER_ISR, FRAME_PC);
    g->blocks++;
    /* the render fills the half the DMA is not playing */
    uint32_t out = half < 0x4a3ed180u ? 0x4a3ed180u : 0x4a3ed080u;
    for (int i = 0; i < 32; i++) {
        int32_t l = (int32_t)(mc_ld32(x, out + 8 * i) << 8) >> 8;
        int32_t r = (int32_t)(mc_ld32(x, out + 8 * i + 4) << 8) >> 8;
        left[i] = (float)l * (1.0f / 8388608.0f);
        right[i] = (float)r * (1.0f / 8388608.0f);
    }
    return st;
}

/* Tempo, as FUN_40058bb4 / FUN_40091780 set it: units of BPM x 120
 * (30-300 BPM -> 3600-36000). The render advances its musical clock by
 * twice this every block (0x40149310). The clock source becomes internal
 * (0x40149314 bit 0; a saved state may follow external MIDI clock, and with
 * none arriving the clock sits at the 300 BPM maximum). */
#define TEMPO_SET 0x40140a84u
#define TEMPO_FOLLOW 0x40140a88u
#define CLOCK_RATE 0x40149310u
#define CLOCK_SOURCES 0x40149314u

void mc_engine_set_tempo(mc_engine *g, double bpm)
{
    long t = (long)(bpm * 120.0 + 0.5);
    if (t < 3600) t = 3600;
    if (t > 36000) t = 36000;
    mc_st32_raw(g->x, CLOCK_SOURCES, 1);
    mc_st32_raw(g->x, TEMPO_SET, (uint32_t)t);
    mc_st32_raw(g->x, TEMPO_FOLLOW, 1);
    mc_st32_raw(g->x, CLOCK_RATE, (uint32_t)t);
}

double mc_engine_get_tempo(mc_engine *g)
{
    return mc_ld32(g->x, CLOCK_RATE) / 120.0;
}

int mc_engine_get_param(mc_engine *g, int track, int offset)
{
    uint32_t a = track < 6 ? g->kit + 0x1c + 100 * track + offset : g->kit + 0x274 + offset;
    return (int)(mc_ld16(g->x, a) >> 8);
}

int mc_engine_get_level(mc_engine *g, int track)
{
    return (int)(mc_ld16(g->x, g->kit + 0x10 + 2 * track) >> 8);
}

int mc_engine_get_mute(mc_engine *g, int track)
{
    return (int)((mc_ld32(g->x, MUTE_STATE + 0x14) >> track) & 1);
}

uint32_t mc_engine_kit(mc_engine *g) { return g->kit; }

int mc_engine_peek(mc_engine *g, uint32_t addr)
{
    uint8_t b;
    mc_mem_read(g->x, addr, &b, 1);
    return b;
}

void mc_engine_read(mc_engine *g, uint32_t addr, void *dst, uint32_t n) { mc_mem_read(g->x, addr, dst, n); }
