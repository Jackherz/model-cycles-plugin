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
};

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
    mc_ctx *x = g->x;
    if (track < 0 || track > 6)
        return -1;
    uint32_t v = (uint32_t)value << 8;
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
