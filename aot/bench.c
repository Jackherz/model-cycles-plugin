/*
 * Time the AOT render the way a plugin runs it: one 32-frame block after
 * another, with notes. Worst case from the profiles: six Chord tracks,
 * decay 127, both sends 127, retriggered every quarter second.
 *
 *   bench STATE_DIR [seconds] [out.wav]
 */
#include <mach/mach_time.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "engine.h"

static int cmp(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

static void wav(const char *path, const float *l, const float *r, long n)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t data = (uint32_t)n * 4, riff = 36 + data, rate = 48000, br = rate * 4, fmt = 16;
    uint16_t pcm = 1, ch = 2, ba = 4, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmt, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f);
    fwrite(&br, 4, 1, f); fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    for (long i = 0; i < n; i++) {
        int16_t s[2];
        float v[2] = {l[i], r[i]};
        for (int c = 0; c < 2; c++) {
            float x = v[c] > 1 ? 1 : v[c] < -1 ? -1 : v[c];
            s[c] = (int16_t)(x * 32767);
        }
        fwrite(s, 2, 2, f);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "bench STATE_DIR [seconds] [out.wav]\n"); return 2; }
    double seconds = argc > 2 ? atof(argv[2]) : 10;
    int st;
    mc_engine *g = mc_engine_new(argv[1], &st);
    if (!g) { fprintf(stderr, "load failed %d\n", st); return 1; }
    for (int t = 0; t < 6; t++) {
        mc_engine_param(g, t, 0x26, 5);       /* machine: Chord */
        mc_engine_param(g, t, 0x38, 127);     /* decay */
        mc_engine_param(g, t, 0x3c, 127);     /* delay send */
        mc_engine_param(g, t, 0x3e, 127);     /* reverb send */
    }
    long nblocks = (long)(seconds * 48000 / 32);
    double *dt = malloc(sizeof(double) * nblocks);
    float *L = malloc(sizeof(float) * 32 * nblocks), *R = malloc(sizeof(float) * 32 * nblocks);
    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);
    int bad = 0;
    for (long b = 0; b < nblocks; b++) {
        if (b % 375 == 0)                  /* every 0.25 s */
            for (int t = 0; t < 6; t++)
                mc_engine_note(g, t, 48 + 2 * t, 120, 1, 0x0681);
        uint64_t t0 = mach_absolute_time();
        if (mc_engine_render(g, L + 32 * b, R + 32 * b) != 0) bad++;
        dt[b] = (double)(mach_absolute_time() - t0) * tb.numer / tb.denom / 1e3;   /* us */
    }
    double sum = 0, peak = 0;
    for (long b = 0; b < nblocks; b++) sum += dt[b];
    for (long i = 0; i < 32 * nblocks; i++) peak = fabsf(L[i]) > peak ? fabsf(L[i]) : peak;
    qsort(dt, nblocks, sizeof(double), cmp);
    printf("%ld blocks, %d failed; per 32-frame block: mean %.1f us, p50 %.1f, p99 %.1f, max %.1f "
           "(budget 666.7 us: mean %.1f%%); peak level %.3f\n",
           nblocks, bad, sum / nblocks, dt[nblocks / 2], dt[nblocks * 99 / 100], dt[nblocks - 1],
           100 * sum / nblocks / 666.7, peak);
    if (argc > 3) wav(argv[3], L, R, 32 * nblocks);
    return 0;
}
