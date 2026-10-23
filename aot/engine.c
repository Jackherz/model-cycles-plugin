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
#include <complex.h>
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
    /* plugin-side delay / reverb engines (mc_engine_set_fx) */
    mc_fx_fn fx_fn;
    void *fx_user;
    int fx_on[2];
    float fx_in[2][64];
    struct tf_state {
        double ic1, ic2;             /* the SVF's integrators */
        double g, k, w;              /* coefficients and wet share where the last block ended */
        double env;                  /* the envelope, 1 at a note */
        int16_t c, r, a, d;          /* this block's smoothed words */
        int on, lp, trig;
        /* the Digitone II's filter types (dn2_filter), when type > 0 */
        int type, extra, ran;        /* ran: the type the state below belongs to */
        double key;                  /* filter keytrack: semitones the cutoff moves (plugin) */
        double cf[8];                /* its coefficients where the last block ended */
        double s[4];                 /* ladder / Chamberlin / one-pole states */
        float comb[16384];           /* the comb's delay line */
        int pos;
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
/* right after each call (render built with these hooks too): the
 * effect's output, interleaved stereo Q31 like the bus, 32 frames */
#define FX_HOOK_DELAY_DONE  0x400569a2u
#define FX_HOOK_REVERB_DONE 0x40056a32u
#define DELAY_OUT           0x8000ba90u
#define REVERB_OUT          0x8000bb90u

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

/* The Digitone II's filter machines, re-created from its own DSP code run
 * offline (digitone2-plugin/scripts/filter_response.py, filter_raw*.py:
 * a deterministic noise voice through each type and setting, the complex
 * response fitted; NOTES.md step 31). Not bit-exact: the laws below fit
 * the measured responses within about 0.05 dB (Multimode, Lowpass 4,
 * Equalizer, Legacy at low resonance) to 1-3 dB (the combs with their
 * loop filter closed). FREQ is the cutoff word (k28) over 256, 0-127,
 * plus the envelope; RESO / GAIN / FEEDBACK the reso word (k29); the
 * third knob (MORPH, Q, LPF, LP/HP) is `extra`, set by the plugin. */
enum { FT_CYCLES, FT_MULTI, FT_LP4, FT_EQ, FT_COMBM, FT_LEGACY, FT_COMBP };

static double dn2_hz(double v) { return 5.0 * exp(0.065606 * v); }   /* 64 = 333 Hz, 127 = 20.8 kHz */

static double interp(const double *t, double x, int n, double step)
{   /* t[0..n-1] at 0, step, 2 step ...; the last entry at 127 */
    if (x <= 0) return t[0];
    double i = x / step;
    int j = (int)i;
    if (j >= n - 2) {               /* between the last regular point and 127 */
        double x0 = step * (n - 2), fr = (x - x0) / (127.0 - x0);
        fr = fr > 1 ? 1 : fr;
        return t[n - 2] + (t[n - 1] - t[n - 2]) * fr;
    }
    return t[j] + (t[j + 1] - t[j]) * (i - j);
}

/* Multimode's damping k over RESO (0, 8 .. 120, 127): Q 0.5 .. 19.7 */
static const double MM_K[17] = {2.0015, 1.7245, 1.2227, 0.8234, 0.5641, 0.4023, 0.2978, 0.2275, 0.1788,
                                0.1442, 0.1187, 0.0994, 0.0844, 0.0729, 0.0636, 0.0560, 0.0507};
/* Legacy (a Chamberlin filter): F at RESO 0 and its damping q at RESO 0,
 * over FREQ 0, 8 .. 120, 127; F rises toward 2 sin(pi fc / fs) with RESO */
static const double LEG_F0[17] = {0.00085, 0.00130, 0.00207, 0.00336, 0.00550, 0.00921, 0.01526, 0.02553, 0.04273,
                                  0.07078, 0.11721, 0.19011, 0.29857, 0.45216, 0.63942, 0.82279, 0.95};
static const double LEG_Q0[17] = {1.98, 1.98, 1.98, 1.98, 1.98, 1.98, 1.98, 1.98, 1.98,
                                  1.89, 1.89, 1.81, 1.72, 1.57, 1.37, 1.19, 1.05};
/* the combs: the loop's damping cutoff over LPF (Hz: the DN2's one-pole
 * coefficients 0.82 .. 0.03 as cutoffs, -ln(p) fs / 2 pi), the output trim
 * over FEEDBACK */
static const double COMB_FC[9] = {1516, 1898, 3175, 5296, 8470, 14000, 19300, 21000, 21000};
static const double COMB_C[9] = {1.0, 1.0, 1.0, 1.0, 1.0, 0.99, 0.945, 0.78, 0.65};

static void dn2_coefs(int type, double v, double r, double x3, double *c)
{
    switch (type) {
    case FT_MULTI: {                /* TPT SVF; MORPH: LP -> BP -> HP, equal power */
        c[0] = tan(M_PI * dn2_hz(v) / 48000.0);
        c[1] = interp(MM_K, r, 17, 8.0);
        double u = x3 / 127.0 * 2.0, lo = u < 1 ? u : u - 1;
        double th = M_PI / 2 * (lo - 0.04 * sin(2 * M_PI * lo));
        if (u < 1) c[2] = cos(th), c[3] = sin(th), c[4] = 0;
        else c[2] = 0, c[3] = cos(th), c[4] = sin(th);
        break;
    }
    case FT_LP4:                    /* zero-delay ladder, resonance 3.45 (r/127)^2, gain-compensated */
        c[0] = tan(M_PI * dn2_hz(v) / 48000.0);
        c[1] = 3.45 * (r / 127.0) * (r / 127.0);
        break;
    case FT_EQ: {                   /* a bell: +-12 dB, Q 0.5 at 64 */
        double A = pow(10.0, (r - 64.0) * 3.0 / 16.0 / 40.0);
        double Q = 0.5 * exp(0.0287 * (x3 - 64.0));
        c[0] = tan(M_PI * dn2_hz(v) / 48000.0);
        c[1] = 1.0 / ((Q < 0.08 ? 0.08 : Q) * A);       /* k' */
        c[2] = A * A - 1.0;
        break;
    }
    case FT_COMBM:
    case FT_COMBP: {                /* FREQ: semitones, 72 = C4; feedback 1 - (1 - fb)^3 */
        double hz = 261.6256 * pow(2.0, (v - 72.0) / 12.0), d = 48000.0 / hz;
        double fb = 1.0 - pow(1.0 - r / 127.0, 3.0), lim = type == FT_COMBP ? 0.997 : 0.951;
        c[0] = d < 3 ? 3 : d > 16380 ? 16380 : d;   /* 3: Hermite reads a sample past the pair */
        c[1] = (fb > lim ? lim : fb) * (type == FT_COMBP ? 1 : -1);
        c[2] = tan(M_PI * interp(COMB_FC, x3, 9, 16.0) / 48000.0);   /* the damping SVF's g */
        {   /* in tune: the damping filter delays the loop too, by its phase
             * delay at the comb's pitch; the delay line makes up the rest */
            const double w0 = 2 * M_PI * hz / 48000.0;
            const double complex z1 = cexp(-I * w0), sd = (1 - z1) / (1 + z1) / c[2];
            const double tau = -carg(1 / (sd * sd + 1.41421356 * sd + 1)) / w0;
            c[0] -= tau;
            c[0] = c[0] < 3 ? 3 : c[0];
        }
        c[3] = interp(COMB_C, r, 9, 16.0);
        break;
    }
    case FT_LEGACY: {               /* Chamberlin; TYPE < 64 LP, else HP (= input - LP) */
        double f0 = interp(LEG_F0, v, 17, 8.0), hz = dn2_hz(v);
        double f1 = v >= 112 ? (v >= 120 ? 1.398 + (v - 120) * 0.03 : 0.944 + (v - 112) * 0.0567)
                             : 2 * sin(M_PI * (hz < 20000 ? hz : 20000) / 48000.0);
        if (f1 < f0) f1 = f0;
        double F = f0 + (f1 - f0) * (1 - exp(-r / 33.0));
        c[0] = F > 1.9 ? 1.9 : F;
        c[1] = interp(LEG_Q0, v, 17, 8.0) * exp(-0.02925 * r);
        c[2] = x3 >= 64;
        break;
    }
    }
}

static void dn2_filter(mc_engine *g, int t, struct tf_state *f)
{
    /* FREQ steps are 1.1358 semitones each (0.065606 nepers) */
    double v = f->c / 256.0 + (f->a - 0x4000) / 256.0 * f->env + f->key / 1.1358;
    v = v < 0 ? 0 : v > 127 ? 127 : v;
    const double r = (f->r < 0 ? 0 : f->r) / 256.0;
    double cn[8] = {0};
    dn2_coefs(f->type, v, r > 127 ? 127 : r, f->extra, cn);
    if (f->ran != f->type) {       /* a new type: from rest, its coefficients as they are */
        memset(f->s, 0, sizeof f->s);
        memset(f->comb, 0, sizeof f->comb);
        f->ic1 = f->ic2 = 0;
        f->pos = 0;
        memcpy(f->cf, cn, sizeof cn);
        f->ran = f->type;
        f->on = 0;
    }
    uint8_t *buf = g->x->mem + TRACK_BUFS + 128 * t;
    const double norm = 1.0 / 2147483648.0;
    double c[8], dc[8];
    for (int i = 0; i < 8; i++)
        c[i] = f->cf[i], dc[i] = (cn[i] - f->cf[i]) / 32;
    if (f->type == FT_LEGACY)
        dc[2] = 0, c[2] = cn[2];    /* LP/HP switches, it does not ramp */
    double ic1 = f->ic1, ic2 = f->ic2, s0 = f->s[0], s1 = f->s[1], s2 = f->s[2], s3 = f->s[3];
    for (int i = 0; i < 32; i++) {
        for (int j = 0; j < 8; j++)
            c[j] += dc[j];
        const double x = (int32_t)mc_ld32m(buf, 4 * i) * norm;
        double y;
        switch (f->type) {
        case FT_MULTI:
        case FT_EQ: {
            const double gg = c[0], kk = c[1];
            const double a1 = 1.0 / (1.0 + gg * (gg + kk)), a2 = gg * a1, a3 = gg * a2;
            const double v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2 * v1 - ic1;
            ic2 = 2 * v2 - ic2;
            if (f->type == FT_MULTI)
                y = c[2] * v2 + c[3] * v1 + c[4] * (x - kk * v1 - v2);
            else
                y = x + c[2] * kk * v1;
            break;
        }
        case FT_LP4: {
            const double gg = c[0], rr = c[1], G = gg / (1 + gg);
            /* each stage v = (u - s) G, y = v + s, s' = y + v; the
             * feedback solved: y4 = G^4 u + S */
            const double S = G * G * G * s0 / (1 + gg) + G * G * s1 / (1 + gg) + G * s2 / (1 + gg) + s3 / (1 + gg);
            double u = (x - rr * S) / (1 + rr * G * G * G * G);
            double st[4] = {s0, s1, s2, s3};
            for (int k = 0; k < 4; k++) {
                const double vv = (u - st[k]) * G, yy = vv + st[k];
                st[k] = yy + vv;
                u = yy;
            }
            s0 = st[0], s1 = st[1], s2 = st[2], s3 = st[3];
            y = (1 + rr) * u;
            break;
        }
        case FT_COMBM:
        case FT_COMBP: {
            /* The DN2's laws (tuning, feedback, LPF, trim), with a better
             * sound, after the Comb Filter machine of Tim Cox's "Work"
             * (Schwung, MIT; NOTES.md step 36): the delay read with 4-point
             * Hermite interpolation (in tune, the highs kept), damped by a
             * 2-pole state-variable low-pass, and soft-saturated as it is
             * fed back, so high feedback rings warm instead of harsh. */
            const double d = c[0], gfb = c[1], gg = c[2], trim = c[3];
            double rp = f->pos - d;
            if (rp < 0) rp += 16384;
            const int i0 = (int)rp;
            const double fr = rp - i0;
            const double ym = f->comb[(i0 - 1) & 16383], y0 = f->comb[i0], y1 = f->comb[(i0 + 1) & 16383],
                         y2 = f->comb[(i0 + 2) & 16383];
            const double c1 = 0.5 * (y1 - ym), c2 = ym - 2.5 * y0 + 2 * y1 - 0.5 * y2,
                         c3 = 0.5 * (y2 - ym) + 1.5 * (y0 - y1);
            const double del = ((c3 * fr + c2) * fr + c1) * fr + y0;
            /* the damping: a TPT SVF low-pass, Q 0.707 (k = sqrt 2) */
            const double kk = 1.41421356, a1 = 1.0 / (1.0 + gg * (gg + kk)), a2 = gg * a1, a3 = gg * a2;
            const double v3 = del - s1, v1 = a1 * s0 + a2 * v3, v2 = s1 + a2 * s0 + a3 * v3;
            s0 = 2 * v1 - s0;
            s1 = 2 * v2 - s1;
            /* fed back through Work's soft clip, x (27 + x^2) / (27 + 9 x^2) */
            double fbk = x + gfb * v2;
            fbk = fbk < -3 ? -1 : fbk > 3 ? 1 : fbk * (27 + fbk * fbk) / (27 + 9 * fbk * fbk);
            f->comb[f->pos] = (float)fbk;
            f->pos = (f->pos + 1) & 16383;
            y = trim * (x + 1.05 * gfb * v2);
            break;
        }
        default: {                  /* FT_LEGACY */
            const double F = c[0], q = c[1];
            const double lpn = s0 + F * s1;              /* lp[n] = lp[n-1] + F bp[n-1] */
            const double hp = x - lpn - q * s1;
            s1 = s1 + F * hp;
            s0 = lpn;
            y = c[2] ? x - lpn : lpn;
            break;
        }
        }
        double o = y * 2147483648.0;
        o = o > 2147483647.0 ? 2147483647.0 : o < -2147483648.0 ? -2147483648.0 : o;
        mc_st32m(buf, 4 * i, (uint32_t)(int32_t)o);
    }
    if (fabs(ic1) < 1e-20) ic1 = 0;
    if (fabs(ic2) < 1e-20) ic2 = 0;
    for (int k = 0; k < 4; k++)
        if (fabs(f->s[k]) < 1e-20) f->s[k] = 0;
    f->ic1 = ic1, f->ic2 = ic2, f->s[0] = s0, f->s[1] = s1, f->s[2] = s2, f->s[3] = s3;
    memcpy(f->cf, cn, sizeof cn);
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
        if (f->type != FT_CYCLES) {
            dn2_filter(g, t, f);
            continue;
        }
        f->ran = FT_CYCLES;
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
        /* + the keytrack: 112.81 index units a semitone (5 Hz x 4400^(i/16384)) */
        double idx = (lp ? f->c : now) + (f->a - 0x4000) * f->env + f->key * 112.81;
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

/* Plugin-side effects: at each effect's call the bus is its input (the
 * host input already added); after the call, the firmware's own output is
 * replaced by the plugin's. The firmware's effect keeps running on the same
 * input, so switching back to it is seamless. */
static void fx_engine_hook(mc_engine *g, uint32_t pc)
{
    const int which = pc == FX_HOOK_DELAY || pc == FX_HOOK_DELAY_DONE ? 0 : 1;
    if (!g->fx_on[which] || !g->fx_fn)
        return;
    const double norm = 1.0 / 2147483648.0;
    if (pc == FX_HOOK_DELAY || pc == FX_HOOK_REVERB) {
        for (int i = 0; i < 64; i++)
            g->fx_in[which][i] = (float)((int32_t)mc_ld32(g->x, SEND_BUS + 4 * i) * norm);
        return;
    }
    float fw[64], out[64];
    const uint32_t at = which ? REVERB_OUT : DELAY_OUT;
    for (int i = 0; i < 64; i++)
        fw[i] = (float)((int32_t)mc_ld32(g->x, at + 4 * i) * norm);
    g->fx_fn(g->fx_user, which, g->fx_in[which], fw, out);
    for (int i = 0; i < 64; i++) {
        double o = (double)out[i] * 2147483648.0;
        o = o > 2147483647.0 ? 2147483647.0 : o < -2147483648.0 ? -2147483648.0 : o;
        mc_st32_raw(g->x, at + 4 * i, (uint32_t)(int32_t)o);
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
    if (pc == FX_HOOK_DELAY || pc == FX_HOOK_REVERB)
        fx_hook(x, pc);   /* the host's input onto the bus first */
    fx_engine_hook(g, pc);
}

static void engine_update_hook(mc_engine *g)
{
    g->x->hook = g->in_l || g->smooth_filter || g->fx_on[0] || g->fx_on[1] ? engine_hook : NULL;
    g->x->hook_user = g;
}

void mc_engine_set_fx(mc_engine *g, mc_fx_fn fn, void *user, int delay_on, int reverb_on)
{
    g->fx_fn = fn;
    g->fx_user = user;
    g->fx_on[0] = delay_on && fn;
    g->fx_on[1] = reverb_on && fn;
    engine_update_hook(g);
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
    for (int t = 0; t < 6; t++) {    /* from rest, keeping each track's type */
        const int type = g->tf[t].type, extra = g->tf[t].extra;
        memset(&g->tf[t], 0, sizeof g->tf[t]);
        g->tf[t].type = type, g->tf[t].extra = extra;
    }
    engine_update_hook(g);
}

double mc_filter_db(int type, double v, double r, double extra, double hz)
{
    double c[8] = {0};
    if (type <= FT_CYCLES || type > FT_COMBP)
        return 0.0;
    dn2_coefs(type, v, r, extra, c);
    const double w = 2 * M_PI * hz / 48000.0;
    const double complex z1 = cexp(-I * w), sb = (1 - z1) / (1 + z1);   /* bilinear s, before / g */
    double complex h;
    switch (type) {
    case FT_MULTI: {
        const double complex s = sb / c[0], den = s * s + c[1] * s + 1;
        h = (c[2] + c[3] * s + c[4] * s * s) / den;
        break;
    }
    case FT_EQ: {
        const double complex s = sb / c[0], den = s * s + c[1] * s + 1;
        h = 1 + c[2] * c[1] * s / den;
        break;
    }
    case FT_LP4: {
        const double complex G = 1 / (1 + sb / c[0]), G4 = G * G * G * G;
        h = (1 + c[1]) * G4 / (1 + c[1] * G4);
        break;
    }
    case FT_COMBM:
    case FT_COMBP: {
        /* the delay (Hermite: close to exact), the damping SVF (Q 0.707) */
        const double complex zd = cexp(-I * w * c[0]);
        const double complex sd = sb / c[2], L = 1 / (sd * sd + 1.41421356 * sd + 1);
        const double complex e = L * zd / (1 - c[1] * L * zd);
        h = c[3] * (1 + 1.05 * c[1] * e);
        break;
    }
    default: {                      /* Legacy: the Chamberlin's LP, or 1 - LP */
        const double F = c[0], q = c[1];
        const double complex den = (1 - z1) * (1 - z1 + F * q * z1) + F * F * z1, lp = F * F * z1 / den;
        h = c[2] ? 1 - lp : lp;
        break;
    }
    }
    return 20 * log10(cabs(h) + 1e-12);
}

void mc_engine_set_filter_key(mc_engine *g, int track, double semitones)
{
    if (track >= 0 && track <= 5)
        g->tf[track].key = semitones;
}

void mc_engine_set_filter(mc_engine *g, int track, int type, int extra)
{
    if (track < 0 || track > 5)
        return;
    g->tf[track].type = type < 0 ? 0 : type > FT_COMBP ? FT_COMBP : type;
    g->tf[track].extra = extra < 0 ? 0 : extra > 127 ? 127 : extra;
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
