/* CyclesEmu's wrappers around the Schwung audio effects vendored next to
 * this folder (each with its own MIT licence and SOURCE.txt). Each module's
 * source is included unchanged into its own wrapper, its one exported entry
 * point renamed, and driven through its own API: CloudSeed, PSX Verb and
 * Space Echo through Schwung's v2 effect API (its parameters), their blocks
 * on float samples (each module's v2 block, copied without the 16-bit step);
 * CHOMPI TEMPO's dual delay and its reverb on their float path. All run at
 * 48 kHz. */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cyem_fx {
    void *(*create)(void);
    void (*destroy)(void *);
    void (*set)(void *, const char *key, const char *val);
    void (*process)(void *, float *interleaved, int frames);   /* in place, -1..1 */
} cyem_fx;

const cyem_fx *cyem_fx_cloudseed(void);
const cyem_fx *cyem_fx_psxverb(void);
const cyem_fx *cyem_fx_spacecho(void);

/* CHOMPI TEMPO's dual delay (granularDelay, its compressor and the reverb
 * of the Wand's up side): wand -1..1 (0 off; down a clean tempo-synced
 * delay, 1 bar to 1/16; up melting into reverb), random 0..1, feedback 0..1.
 * Wet only. Allocates 7.7 MB at create (not on the audio thread). */
void *cyem_chompi_create(void);
void cyem_chompi_destroy(void *);
void cyem_chompi_set(void *, float wand, float random, float feedback);
void cyem_chompi_process(void *, const float *in, float *out, int frames, double bpm);   /* interleaved */

/* CHOMPI TEMPO's reverb alone (Mutable Instruments' Clouds/Rings reverb):
 * time, lowpass and diffusion 0..1. Wet only. */
void *cyem_tempoverb_create(void);
void cyem_tempoverb_destroy(void *);
void cyem_tempoverb_set(void *, float time, float lowpass, float diffusion);
void cyem_tempoverb_process(void *, const float *in, float *out, int frames);   /* interleaved */

#ifdef __cplusplus
}
#endif
