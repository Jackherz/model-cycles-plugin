/* The Model:Cycles engine on the AOT render (engine.c). One instance per
 * mc_engine; instances are independent. Call everything for one instance
 * from one thread (the audio thread). */
#ifndef MC_ENGINE_H
#define MC_ENGINE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct mc_engine mc_engine;

/* State from aot/dump_state.py: DIR/regions.bin and DIR/cpu.bin. */
mc_engine *mc_engine_new(const char *dir, int *err);
void mc_engine_free(mc_engine *g);
/* A sound parameter of track 0-5 by its record offset (NOTES.md CC table),
 * or an FX parameter with track 6 and its FX offset (0x12 delay time ...).
 * value 0-127, already clamped as the firmware clamps it. */
int mc_engine_param(mc_engine *g, int track, int offset, int value);
/* as mc_engine_param, with the whole 16-bit word (value << 8 plus a fraction
 * in the low byte): for pitch glides finer than a semitone */
int mc_engine_param_word(mc_engine *g, int track, int offset, int word);
int mc_engine_level(mc_engine *g, int track, int value);
int mc_engine_mute(mc_engine *g, int track, int muted);
int mc_engine_note(mc_engine *g, int track, int note, int velocity, int on, uint32_t trig_flags);
int mc_engine_render(mc_engine *g, float *left, float *right);   /* 32 frames, 48 kHz */
/* Tempo in BPM (30-300), on the engine's internal clock. */
void mc_engine_set_tempo(mc_engine *g, double bpm);
double mc_engine_get_tempo(mc_engine *g);
/* Current values (0-127) as stored in the kit. */
int mc_engine_get_param(mc_engine *g, int track, int offset);
int mc_engine_get_level(mc_engine *g, int track);
int mc_engine_get_mute(mc_engine *g, int track);
/* for checks and tests: the kit's address, and a byte of guest memory */
uint32_t mc_engine_kit(mc_engine *g);
int mc_engine_peek(mc_engine *g, uint32_t addr);
void mc_engine_read(mc_engine *g, uint32_t addr, void *dst, uint32_t n);
/* CyclesFX: from the next mc_engine_render on, add 32 frames of input (the
 * buffers are read during each render; refill them between renders) to the
 * delay send and the reverb send, times the two amounts (0..1). l = NULL
 * stops it. Needs a render translated with --hook 4005698c --hook 40056a1c. */
void mc_engine_set_input(mc_engine *g, const float *l, const float *r, float delaySend, float reverbSend);
/* The track filter run here, its coefficients ramped per sample (engine.c),
 * instead of the firmware's block-stepped one; off (bit-exact) by default.
 * Needs a render translated with --hook 4014ab9c --hook 4014abe2. */
void mc_engine_set_smooth_filter(mc_engine *g, int on);
/* Make a sound word's smoothed copy (what the voices read) equal the word
 * now, skipping the firmware's slide toward it: the glide uses it at a
 * trigger, so a note starts at the pitch it should. */
void mc_engine_snap_param(mc_engine *g, int track, int offset);

#ifdef __cplusplus
}
#endif
#endif
