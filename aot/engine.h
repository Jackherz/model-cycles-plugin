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

#ifdef __cplusplus
}
#endif
#endif
