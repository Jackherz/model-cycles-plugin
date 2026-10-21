/*
 * Measure the engine directly: idle output over time (DC and the rest),
 * and the level of one note per track.
 *   probe STATE_DIR
 */
#include <math.h>
#include <stdio.h>
#include "engine.h"

static void stats(const char *label, mc_engine *g, int seconds, int per_second)
{
    float l[32], r[32];
    double sum = 0, sum2 = 0, peak = 0;
    long n = 0;
    for (long b = 0; b < (long)seconds * 1500; b++) {
        mc_engine_render(g, l, r);
        for (int i = 0; i < 32; i++) {
            sum += l[i]; sum2 += (double)l[i] * l[i]; n++;
            if (fabs(l[i]) > peak) peak = fabs(l[i]);
        }
        if (per_second && (b + 1) % 1500 == 0) {
            double mean = sum / n, ac = sqrt(fmax(0, sum2 / n - mean * mean));
            printf("%s second %3ld: mean (DC) %+.5f  ac rms %.6f  peak %.5f\n", label, (b + 1) / 1500, mean, ac, peak);
            sum = sum2 = peak = 0; n = 0;
        }
    }
    if (!per_second) {
        double mean = sum / n, ac = sqrt(fmax(0, sum2 / n - mean * mean));
        printf("%s: DC %+.5f  ac rms %.5f (%.1f dBFS)  peak %.4f (%.1f dBFS)\n", label, mean, ac,
               20 * log10(ac + 1e-12), peak, 20 * log10(peak + 1e-12));
    }
}

int main(int argc, char **argv)
{
    int err;
    mc_engine *g = mc_engine_new(argv[1], &err);
    if (!g) { printf("load failed %d\n", err); return 1; }
    stats("idle", g, 30, 1);
    for (int t = 0; t < 6; t++) {
        mc_engine *h = mc_engine_new(argv[1], &err);
        mc_engine_note(h, t, 60, 127, 1, 0x0681);
        char lab[64];
        snprintf(lab, sizeof lab, "track %d note 60 vel 127, 2 s", t + 1);
        stats(lab, h, 2, 0);
        mc_engine_free(h);
    }
    return 0;
}
