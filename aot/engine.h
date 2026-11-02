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
/* Raw memory (big-endian, as the emulated CPU sees it), below the
 * peripherals: Twinshot's samples and their table. */
void mc_engine_write(mc_engine *g, uint32_t addr, const void *src, uint32_t n);
/* A track's sound-record word written directly (no firmware call). */
void mc_engine_twin_param(mc_engine *g, int track, int which, int value);   /* 0 Sample 2, 1 Overdrive (unread: the Syntakt applies it outside the machine): 0..127 */
/* CyclesFX: from the next mc_engine_render on, add 32 frames of input (the
 * buffers are read during each render; refill them between renders) to the
 * delay send and the reverb send, times the two amounts (0..1). l = NULL
 * stops it. Needs a render translated with --hook 4005698c --hook 40056a1c. */
void mc_engine_set_input(mc_engine *g, const float *l, const float *r, float delaySend, float reverbSend);
/* Plugin-side delay and reverb engines in place of the firmware's: fn is
 * called each block, after the firmware's effect, with which (0 delay, 1
 * reverb), its input (the send bus at the call: 32 interleaved stereo
 * frames, full scale 1.0), the firmware's own output for the block (fw, the
 * same layout: to measure it, or crossfade from it) and the output to fill,
 * which replaces the firmware's. The delay's output feeds the reverb as the
 * firmware routes it. Needs a render with --hook 400569a2 --hook 40056a32. */
typedef void (*mc_fx_fn)(void *user, int which, const float *in, const float *fw, float *out);
void mc_engine_set_fx(mc_engine *g, mc_fx_fn fn, void *user, int delay_on, int reverb_on);
/* The track filter run here, its coefficients ramped per sample (engine.c),
 * instead of the firmware's block-stepped one; off (bit-exact) by default.
 * Needs a render translated with --hook 4014ab9c --hook 4014abe2. */
void mc_engine_set_smooth_filter(mc_engine *g, int on);
/* With the smooth filter on: a track's filter type, 0 the Cycles' own
 * (Modded-Cycles' LP / OFF / HP), 1-6 the Digitone II's (Multimode,
 * Lowpass 4, Equalizer, Comb-, Legacy, Comb+), and its third knob 0-127
 * (Multimode's morph, the EQ's Q, the combs' LPF, Legacy's LP / HP). */
void mc_engine_set_filter(mc_engine *g, int track, int type, int extra);
/* A Digitone II type's magnitude (dB) at hz, for drawing: FREQ v and RESO
 * r as 0-127 (fractions allowed), its third knob extra. 0 for type 0. */
double mc_filter_db(int type, double v, double r, double extra, double hz);
/* Filter keytracking: the cutoff moved by this many semitones (the plugin
 * sets it at each note: (note - 60) x amount), any type. */
void mc_engine_set_filter_key(mc_engine *g, int track, double semitones);
/* Make a sound word's smoothed copy (what the voices read) equal the word
 * now, skipping the firmware's slide toward it: the glide uses it at a
 * trigger, so a note starts at the pitch it should. */
void mc_engine_snap_param(mc_engine *g, int track, int offset);

/* The Syntakt's amp envelope on a track (NOTES.md step 44): its own DSP
 * function, translated, after the track filter and before level, pan and
 * the sends. Needs syntakt-dsp.bin in the state folder and a build with
 * st_ampenv.c (scripts/syntakt_ampenv.py); mc_engine_amp_available says.
 * ATK, HOLD (127: NOTE), DEC (127: infinite), SUS, REL 0-127; mode 0 AHD,
 * 1 ADSR; aenr: a trig restarts it from 0. Off: the track as it was. */
int mc_engine_amp_available(mc_engine *g);
void mc_engine_set_amp(mc_engine *g, int track, int on, int atk, int hold, int dec, int sus, int rel, int mode, int aenr);
/* note events for the next block: trig (a note), release (its note-off),
 * kill (cut, e.g. a stolen voice) */
void mc_engine_amp_event(mc_engine *g, int track, int trig, int release, int kill);

/* One delay and one reverb for two engines (NOTES.md step 46): the donor's
 * delay and reverb sends are added into the receiver's send buses before
 * its effects run, and the donor's own effects are silenced (its output is
 * its dry tracks). Render the donor first, then the receiver, each block,
 * and sum their outputs. Needs the effect hooks (4005698c, 40056a1c,
 * 400569a2, 40056a32). */
void mc_engine_share(mc_engine *donor, mc_engine *receiver);

#ifdef __cplusplus
}
#endif
#endif
