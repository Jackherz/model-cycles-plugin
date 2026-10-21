# Model:Cycles plugin: working notes

Personal use only. Firmware and anything derived from it (`firmware/`,
`data/`, `notes/*.wav`, Ghidra output) never goes in a public repo.
Brief: `~/Documents/AI/digitone2-plugin/HANDOFF-model-cycles.md`.

## Layout

- `firmware/model-cycles_OS1.13.syx`: sha256 `44fe5862…0640c` (verified,
  matches the tested build). Copied from digiemu's app data
  (`~/Library/Application Support/digiemu/firmware/mc-1.13-44fe5862/`).
- `data/`: copies of the app's snapshots (`resume.snap` plays the factory
  pattern), extracted `sections/` and `plusdrive.img`. Never let a run write
  the app's own copies.
- `re/digiemu`: fresh clone of irpina/digiemu at f57c2f2 (0.4.1).
- `scripts/renderprof.py`: headless block profiler (see below).
- `notes/`: profiles, Ghidra query output, captured audio.
- Ghidra: `~/ghidra-projects/mc-emac` (project `mc-emac`, program
  `section_3_MAIN_OS.bin`, `68000:BE:32:ColdfireEMAC`, base 0x40000400).

## Setup gotchas (2026-10-20)

- **Patched Unicorn.** `uv sync` installs stock Unicorn, which digiemu
  rejects. `tools/install-patched-unicorn.sh` failed with an `Int128` typedef
  error: the real cause is that QEMU's configure aborts when `pkg-config` is
  missing, so it skips the int128 probe. Fix without installing anything:
  a stub `pkg-config` that exits 1 on PATH (Unicorn needs no pkg-config
  libs). Afterwards always run with `uv run --no-sync`, or uv puts stock
  Unicorn back. (The DN2 tree's digiemu venv is also stock now.)
- **Env.** Tools need `DT2_SYX=firmware/model-cycles_OS1.13.syx` and
  `DT2_SECTIONS=data/sections` (renderprof.py sets them).
- **Java/Ghidra.** No system Java: `JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home`.
  Ghidra is 12.1.4 (digiemu scripts default to 12.1.3: pass
  `GHIDRA=`/`GHIDRA_INSTALL_DIR=`). EMAC language installed with
  `tools/ghidra/install-coldfire-emac.sh` (symlinked into
  `~/Library/ghidra/ghidra_12.1.4_PUBLIC/Extensions`).
  Queries: `uv run --no-sync --with pyghidra python tools/ghidraq.py
  --project ~/ghidra-projects/mc-emac --project-name mc-emac section_3_MAIN_OS.bin func ADDR …`
- `tools/guirun.py` cannot load the app's snapshots (its build has no
  `ssi0_dma`); drive `emu.gui.Emulator` directly, as `tools/capbench.py`
  and `scripts/renderprof.py` do.

## Step 1 results: speed and where the instructions go (M1 Pro)

- **digiemu (Unicorn JIT): 0.79–0.83× real time**, unpaced, on
  `resume.snap` playing the factory pattern (`tools/capbench.py --tracks 0`).
  53M of the emulated 64M instr/s. Profile: 80% of wall time inside
  `emu_start` (C), so the Python glue is not the bottleneck.
- **~97% of all emulated instructions are the sound engine.** Block profile
  over one emulated second (`scripts/renderprof.py --instrs 64M`): 53% in
  0x400a7xxx–0x400aaxxx, 43% in 0x40056xxx–0x40058xxx, everything else ~3%.
  The hot blocks run 46.7k / 93k / 140k / 280k times per second, i.e.
  1–6× per sample (six tracks): per-sample DSP loops, not idle.
- **[C] The 42.6k/block figure was wrong** (see step 2): it divided
  digiemu's instruction clock, which includes skipped idle spins
  (`longrun` `fast_idle`) and is not an exact executed count, by the frame
  count. The shares above are of *executed* code and stand.

## Step 2 results: worst-case render cost (2026-10-20)

Method: `scripts/renderprof.py` now counts real instructions per block with
digiemu's decoder (`emu/cfisa.decode`, once per distinct block; checked
against a per-instruction Unicorn code hook: 6,510,145 vs 6,507,592, 0.04%),
and charges everything from entry to `FUN_40058c5e` until the interrupted PC
runs again to that render call. The per-call cost is the same at emulated
clocks of 64M, 128M and 256M (66.5k / 66.4k), so it is real work, not
time-filling. Render calls run at ~1490/s (1500 expected) and are ~98% of
executed instructions.

MIDI control: CC numbers from https://midi.guide/d/elektron/model-cycles/
(machine CC 70, decay 80, delay/reverb send 12/13, mute 94, color/shape
16/17, sweep/contour 18/19, LFO speed/depth 102/109, reverb size 87, delay
feedback 86). **CC 70 values: 0 Kick … 5 Chord, confirmed on screen**
(`notes/mach*.png`). Tracks 1–6 listen on channels 1–6.
renderprof's MIDI goes in on arrival (`MidiIn._track` overridden): the
stock host-time mapping schedules bytes into an emulated future a slowed
run never reaches (0 of 54 delivered before the fix).

Instructions per 32-frame render call (all six tracks on one machine, decay
127, both FX sends 127, a note on every track every 4M clock instructions,
factory pattern still playing):

| case | mean | p99 | max |
|---|---|---|---|
| all six tracks muted (CC 94) | 66.4k | 66.5k | 66.8k |
| factory pattern as restored | 66.5k | 66.6k | 67.5k |
| Kick (0) | 76.1k | 77.2k | 77.3k |
| Snare (1) | 74.7k | 75.6k | 76.0k |
| Metal (2) | 70.4k | 71.2k | 71.5k |
| Perc (3) | 68.4k | 69.1k | 69.3k |
| Tone (4) | 63.9k | 65.5k | 66.5k |
| **Chord (5)** | 81.2k | 82.4k | **82.8k** |
| Chord, color/shape extremes, sweep/contour/LFO/reverb/feedback 127 | 81.1–81.5k | 82.6k | 82.8k |

- **The engine is nearly fixed-cost**: ~66k per block even with everything
  muted (all six voices and the FX always run). Busy voices add at most
  ~16k. **Design budget: 83k instructions per block, ~2,600 per frame,
  ~125M instructions per second of audio.**
- Audio checked: muted RMS 84, sweeps RMS 13.8k–23k (`notes/*.wav`, the
  full-FX runs clip).
- Unicorn really executes ~80M instr/s here (~100M executed per emulated
  second x 0.8 real time).

### Render path (Ghidra + profile)

- Vector 170 (`0x400589c0`): eDMA ch 50 major-loop ISR. Counts, stores the
  DMA position, acks ch 50 (`0xFC04401C = 0x32`), forces INTC1
  INTFRCH bit 31 (`0xFC04C010`), the same handoff as the Digitakt II.
- `FUN_40058c5e`: the forced-interrupt block handler, one 32-frame block.
  Clears the force, picks the output half `0x4a3ed080 + 0x100*half` (32
  frames x 2 x 32-bit), ticks six per-track counters at `0x800017b4` /
  `0x800017cc`, drains an event list from `FUN_40091d80` (switch on event
  type: sequencer/MIDI), then calls `FUN_4005979e(&ctx)` with
  ctx[0] = `0x800015a0`, and afterwards `FUN_40002912(buf, 0x20)` (cache
  flush of the output half, presumably). Block length is `ram0x40149310`.
- `FUN_4005979e`: the render. Callees: `FUN_400567ba`, `FUN_40056d9c`,
  `FUN_40056e24`, `FUN_40056ea8`, `FUN_40056f58`, `FUN_40058474`,
  `FUN_4008f1f0`, `FUN_40091ab2`, `FUN_400a7d4a`. The hottest functions
  (`FUN_400a8204`, `FUN_400a835e`, `FUN_400a8fc2`) have no static callers:
  dispatched through pointers, probably one per machine.
- Engine state is in on-chip SRAM, `0x80000000+` (`0x800015a0`,
  `0x800017ac..0x80001854`).

## Decision so far

Options (a) whole firmware and (b) render alone cost the same, since the
render is ~98% of executed code. Budget for a 32-frame block is 667 µs; the
worst case is 83k instructions:

| engine | executed instr/s | µs per worst block | share of budget |
|---|---|---|---|
| Unicorn (digiemu) | ~80M | ~1040 | 155%, too slow |
| digikit native ColdFire interpreter | ~80M | ~1040 | 155%, too slow |
| needed for 100% / 25% of budget | 125M / 500M | 667 / 167 | |

So interpretation of any kind is out: **(b) driven per block, with the
render (two clusters, about 28 KB of address range) recompiled ahead of time
to native code**, checked bit for bit against digiemu, needs to reach
~500M+ ColdFire instr/s equivalent (~1 host instruction per ColdFire
instruction on a 3 GHz core is ~3G/s, so this is plausible for straight-line
DSP code). (c), a C++ reimplementation, stays the fallback and can reuse the
same reference harness.

## Plan agreed (2026-10-20)

1. Full six-track engine, exactly as the firmware runs it (bit-exact vs
   digiemu), made fast by AOT compilation of the render.
2. Then a single-track mode (run one voice's code, skip the rest) and an
   FX-only mode, as two plugins over the same core: "Cycles voice" (machine
   is a parameter, CC 70) and "Cycles FX" (send effect).
3. Optionally a full six-track instance with six stereo outs.

## Step 3: reference render harness (2026-10-20), bit-exact

- `scripts/capture.py` runs digiemu (via `scripts/emuhost.py`) and records
  consecutive calls of `FUN_40058c5e`: a base dump of every mapped region
  at the first entry; per call the CPU context (`uc.context_save()`, which
  carries the EMAC state Unicorn has no register API for), the pages that
  changed since the previous exit, every peripheral read with its value and
  every peripheral write, and the pages changed at exit. `--trace N` records
  PC + D0-A7 per instruction for lockstep comparison; `--watch ADDR` logs
  who changes a word.
- `scripts/replay.py` runs each call in a bare patched Unicorn (CFV4E,
  `NATIVE_RTE`, digiemu's `edma_sw.SoftwareBank`) from its recorded entry,
  feeds peripheral reads back in order, and compares every touched page,
  the whole DMA regions and the peripheral writes.
- **Result: bit-exact.** Factory pattern 300/300 calls, six Kicks with
  notes 300/300, six Chords with notes 100/100.
- Bare Unicorn, standalone: ~1.9-2.1 ms per block (budget 0.667 ms), so the
  AOT step is still required.

What it took (each was a real divergence):
- **RTE**: stock Unicorn stops on `rte` with EXCP_RTE (256); digiemu enables
  the patched build's `NATIVE_RTE`.
- **The render uses eDMA.** It programs channel 30 (`0xFC0453C0`: SADDR in
  the `0x4a4..0x4a7` 4 MB modulo ring, DADDR in SRAM, scatter-gather via
  DLAST_SGA in SRAM) to stream delay/reverb lines in, and channel 42 to
  write them back, then polls DONE. Found by lockstep trace: identical
  registers for 48.7k instructions, then a load from `0x8000b6a0` differed,
  and a watch showed it changing with no guest store. DMA writes bypass the
  memory hooks, so `0x4a300000-0x4a7fffff` and SRAM `0x80000000-0x800fffff`
  are diffed in full around every call.
- **Pages first touched later** (a MIDI note takes a new path): their entry
  contents are read at first touch, inside the hook, before the access.
- **Interrupts inside the render**: only vector 208 (the panel-scan PIT3
  chain). The capture defers async interrupts while the render runs (as if
  masked; digiemu's sources retry), so every call replays on its own. No
  synchronous traps occur in the render.
- Tool gotchas: Unicorn's Python binding does not support
  `UC_HOOK_MEM_READ_AFTER` (UC_ERR_ARG); digiemu's peripheral read hooks run
  before ours (registration order), so the value is already in memory in a
  plain READ hook. Never call into Unicorn from another thread while it runs
  (QEMU "code should not be reached" abort): do it inside hooks.

### What the rest of the firmware changes between calls

Entry deltas over 300 base / 100 chord calls, outside the DMA regions:
- `0x40a78xxx`, every call, 2 bytes: the vector-170 ISR's counters and
  DMA position (`0x40a78e04..0x40a78e0c`).
- `0x404a8xxx`, every call, ~11 bytes: the interrupted task's stack (the
  exception frame). Harness plumbing, not engine input.
- Occasional: `0x404d1xxx` (~230 bytes when it changes), `0x40fdexxx`,
  `0x40fbfxxx`, `0x40fdfxxx`, `0x40a79xxx` (81 of 100 chord calls, 3 bytes),
  `0x4049fxxx`, `0x404a9xxx`, `0x422fexxx`. These carry notes and parameter
  changes to the engine: the interface step 4 has to map.

## Step 4: engine inputs mapped, driven bit-exact (2026-10-20)

**Result: with the transport stopped, the render can be driven from MIDI
with no firmware tasks, RTOS or UI, bit-exact.** `scripts/driven.py
--synth ../../data/cap-driven`: 200/200 blocks, with notes on three tracks,
a note-off, decay and delay-time CCs, a machine change and a mute. Only the
engine calls below are applied between blocks; the CPU context (one fixed
one), stack, ISR words and every peripheral read are generated.
`scripts/engine_inputs.py` turns MIDI into those calls, and for all 8
messages it produced exactly the 11 actions the firmware performed.

### The interface

- **Note on/off**: `FUN_4005894a(request)`, an immediate trig event (pool
  alloc `FUN_40091eb6`, queued by `FUN_40092116` on the list at
  `0x40fe15fc` the render drains). The 0x38-byte request, as words: [0]
  track, [1] note, [2] velocity, [3] 1 on / 2 off, [4] flags
  `0x30000|1|pattern track flags` (0x0681 on the factory pattern; read by
  `FUN_4008171e` at `pattern + track*0x2d2 + 0x2c6`), [5] length -1,
  [9] -1, [10] 0x7f; note off: [3]=2, [5]=-1, rest 0. MIDI handlers:
  `FUN_4008171e(track, note, vel, source, ...)` on,
  `FUN_4008145e(track, note, source)` off (they also post to the UI task and
  the recorder; not needed by the engine).
- **Parameters**: every CC is a kit write (value << 8) plus
  `FUN_40059534(value << 8, track, index)`, which stores it in the engine
  block in SRAM at `0x800015a2 + 2*(6 + 33*track + index)` (track 6 = FX,
  at `0x800015a2 + 2*(0xcc + index)`) and drops the track's cached sound
  pointer (`0x800015a0 + 4*(0x93 + track)`). Index = (record offset - 0x14)/2.
  Track level: kit `+0x10 + 2*track` and `FUN_40059594(track, value << 8)`.
  Mute: `FUN_400594f0(state)`, masks at state+0x14/+0x18 -> `0x800017ac/b0`.
- **Kit** at `*(0x800017e4)` (`0x409cd3ec`, "Fluid Simul"): header (name,
  track levels at +0x10), six 100-byte sound records from +0x1c (name, then
  the parameter words), FX block from +0x274.

| CC | parameter | record offset | | CC | parameter | offset |
|---|---|---|---|---|---|---|
| 102 | LFO speed | +0x16 | | 16 | color | +0x2a |
| 103 | LFO multiplier (0-23) | +0x18 | | 17 | shape | +0x2c |
| 104 | LFO fade | +0x1a | | 18 | sweep | +0x2e |
| 106 | LFO waveform (0-6) | +0x1e | | 19 | contour | +0x30 |
| 107 | LFO start phase | +0x20 | | 66 | punch (0/1) | +0x32 |
| 108 | LFO reset (0-4) | +0x22 | | 67 | gate (0/1) | +0x34 |
| 109 | LFO depth | +0x24 | | 80 | decay | +0x38 |
| 70 | machine (0-5) | +0x26 | | 7 | volume+dist | +0x3a |
| 65 | pitch (40-88) | +0x28 | | 12/13 | delay/reverb send | +0x3c/+0x3e |
| 10 | pan | +0x40 | | 85/86/87/88 | delay time/feedback, reverb size/tone | FX +0x12/14/16/18 |

  Not engine: swing (15), chance (14). CC 105 (LFO destination) wrote
  nothing for values 37 and 90: to sweep. Clamps measured at two values
  only (`scripts/ccmap.py`).
- **Per block** (a plugin generates these): vector-170 words at
  `0x40a78e04` (0, counter, 1); an exception frame `0x42fc2608,
  0x40058a08` on our own stack; peripheral reads `0xFC04C010` ->
  0x80000000, `0xFC045640` (TCD50 SADDR) alternating 0x4a3ed080 /
  0x4a3ed180, `0xFC0453DE` (TCD30 CSR) -> 0x80 (DONE), anything else 0.
  The render writes the ring half the DMA is not playing.

### Tools

- `scripts/emuhost.py`: shared headless digiemu runner (MIDI on arrival).
- `scripts/ccmap.py`: CC -> kit and SRAM writes, with writer PCs.
- `scripts/inputwatch.py`: writes a MIDI message causes, against a quiet
  window.
- `scripts/capture.py --stop --midi CALL:HEX`: also logs the engine calls
  and kit writes between blocks (`actions`), the ISR words and the frame.
- `scripts/driven.py [--synth]`: the driven replay above.

## Lessons from other Elektron emulation plugins (read 2026-10-21)

Read, not built: shnolk/monomodule (Monomachine, AGPL-3), radekdymacz/mdmm
(Machinedrum + Monomachine editors, GPL-3; its
`doc/modern-ux/RESEARCH-emulation-cpu.md` is a detailed CPU study) and
joelanders/gearmulator-md-mm (GPL-3). Ideas only; copying their code would
bring their licences with it.

- **Monomodule runs only the sound engine**, as we now do. The host CPU is
  reimplemented in C++ ("host model"), the DSP56300 runs the firmware's own
  track pass under dsp56300's JIT, driven by a tiny injected stub: wait for a
  52-word parameter block, jump into the kernel's track pass, one-word patch
  at its end to divert to the output. One engine per track: One = 1 engine,
  Six = 6 engines rendered one after another (no threads), FX = the FX
  machines on the side-chain. Events at 16-frame block boundaries.
  Resampling only when the host rate differs from the engine's (44.1 kHz).
- **mdmm / gearmulator-md-mm run the whole machine** (ColdFire host in a
  Musashi interpreter, two DSP56300s under the JIT, one deterministic
  single-thread scheduler that steps whichever chip is furthest behind).
  After months of tuning: about 40-55 % of an M4 Pro core per instance at
  48 kHz / 128. Their levers, by size: skipping DSP idle/poll loops (the
  firmware never WAITs), ColdFire idle-branch batching, fewer per-instruction
  checks in the ColdFire loop, ThinLTO (6-10 %), PGO (5-7.5 %, on C++ only).
  Rejected by them: a chip on a worker thread (total CPU up 30-80 % from
  spinning on sync), JIT for the ColdFire (their cost was the wrapper, not
  the 3-4 ns/instruction interpreter), longer JIT blocks (not bit-exact).
- **How they measure**: retired host instructions and cycles per audio frame
  on the emulation thread (`thread_selfcounts`), not wall time (the clock
  moved 2.5-3.9 GHz between runs, ±20 % on a DAW meter); paired ABBA runs;
  golden hashes of audio and RAM that every lever must keep; one switch per
  lever; a release gate "p50 <= 90 % of a 48 kHz / 128-frame callback".
- Their Musashi ColdFire core is the V2 ISA_A subset: **no EMAC**, which our
  render uses throughout.

What this means for us:
1. Our idle problem is already gone: we call the render directly, and the
   render is ~98 % real work. Idle skipping, their biggest lever, has
   nothing left to win here.
2. Their per-block dispatcher and peripheral checks (5.5-9 ns per JIT block,
   ~27 % of their time in peripheral ticks) do not apply either: our render
   has no asynchronous peripherals mid-call (driven.py proved the whole
   interface: three reads and the eDMA copy). An AOT can make the eDMA start
   a plain memcpy and drop the DONE poll.
3. Interpreter speed for reference: 3-4 ns/instruction (Musashi, M4 Pro) ->
   83k instructions = 0.25-0.33 ms a block, 40-50 % of the budget for all six
   tracks. Workable only as a fallback. AOT target: under 1 ns/instruction.
4. Copy their JIT techniques into the AOT: guest registers in host
   registers, condition codes computed only where read (ColdFire sets them
   on almost every instruction), code specialised on mode bits (dsp56300's
   SR mode <-> our EMAC MACSR, which the render sets), direct branches.
5. Per-track engines are what let a DAW spread work over cores (one instance
   per channel strip). Monomodule's Six renders its six sequentially; with
   the per-track split our Six could do the same, our One costs ~1/6.
6. Build the generated C with -O3, ThinLTO and PGO trained by replay.py.
7. Audio-thread hygiene: no allocation or locks in processBlock, one shared
   resampler filter, resample only away from 48 kHz, report the latency.
8. Measure like they do: instructions and cycles per block, ABBA pairs,
   golden hashes (our replay/driven harnesses are those goldens).

## Step 5: the AOT render (2026-10-21), bit-exact, 11 % of the budget

**Result.** The render compiled ahead of time to C runs **bit-exact against
digiemu** on every capture (`aot/check_replay.py`: base 300/300, chord
100/100, kick 300/300, driven 200/200) and, driven from MIDI alone with
everything synthesised (`aot/check_driven.py`), matches the engine state and
the output ring on 200/200 blocks. **Pure C, worst case (six Chord tracks,
decay and both sends 127, retriggered every 0.25 s): mean 73 us per
32-frame block, p50 69, p99 131 (11 % of the 666.7 us budget)**
(`aot/build/bench`, M1 Pro, -O2). Unicorn standalone was ~2 ms.

### How it is built (`aot/`)

- `qemu_table.py`: QEMU's own opcode -> handler table for the CFV4E feature
  set (replays `register_m68k_insns`), so decoding matches Unicorn.
- `coverage.py` / `../scripts/coverage_batch.sh`: blocks the render runs over
  97 scenarios (machines x color/shape, LFO waveforms x destinations, ...):
  991 blocks, converged (late scenarios add 1-4).
- `inventory.py`: 7.1k static instructions, 50 QEMU handlers; EMAC is 37 %
  of executed instructions.
- `translate.py`: ColdFire -> one C function. Each emitter mirrors its
  DISAS_INSN in the patched QEMU (`translate.c`, `helper.c`), quirks included
  (pending (An)+/-(An) writebacks seen by later operands, MAC specialised on
  MACSR at run time, mvz.b setting N from bit 7, ColdFire DIVUL writing only
  the remainder, ...). Registers, flags (QEMU's flushed CC form) and EMAC
  state are C locals, so clang deletes unread flag work. Calls push the real
  return address plus a shadow (address, label) entry; `rts` returns by
  computed goto when they agree, else through a dispatch switch. Roots: the
  coverage blocks and the four engine entry points; static discovery adds
  the rest: 11,353 instructions, 2,086 labels, 70k lines of C, ~4 min at -O2.
  Unimplemented: one undefined opcode at 0x400c9afa (uncovered). An
  indirect target outside the compiled set returns MC_UNKNOWN_TARGET.
- `runtime.h/.c`: guest memory is one 4 GB reservation (address = base +
  guest address, byte-swapped); stores >= 0x8C000000 go through
  `mc_mmio_write` (log, and eDMA: digiemu's edma_sw ported, run synchronously
  on START, DONE set); EMAC helpers from QEMU helper.c (GPL-2.0+).
- `engine.c/.h`: the plugin-facing core: `mc_engine_load` (state from
  `dump_state.py`), `mc_engine_param` (kit word + setparam),
  `mc_engine_level`, `mc_engine_note` (the trig request), `mc_engine_render`
  (one block: ISR words, frame, peripheral values, run, read the half the
  render filled: 24-bit right-justified in 32-bit words, L/R interleaved).
- Checks: `check_replay.py` (captures), `check_driven.py` (MIDI-driven),
  `bench.c`. `build.sh [--trace]` regenerates and compiles.

### Found on the way

- **`scripts/driven.py` was contaminated** until this step: `Replay.call`
  writes the capture's recorded entry pages, so "200/200 driven" in step 4
  still had digiemu's memory applied. Fixed (`entry={}`). Honestly driven,
  Unicorn and the AOT agree with each other everywhere, and with digiemu on
  all engine state and audio; they differ from digiemu only on 3 of 200
  blocks (the note-ons), on two RTOS pages (`0x404a9000`, `0x404d1000`) and
  one write (`0xFC050014 |= 0x2000`): the render posts a UI notification
  (`FUN_40001fba`, a ring that overwrites and a semaphore signal) and in
  digiemu a task was waiting for it. Nothing in the sound depends on it, and
  the post never blocks, so the plugin needs no consumer.
- Unicorn's context: EMAC state at fixed offsets (ACC0-3 at 320, MACSR 352,
  MASK 356 of the 564-byte context); found by loading known values.
- Old captures `cap-test` and `cap-trace` predated the DMA-region diffs and
  were deleted.

## Step 6: the plugin (2026-10-21), CyclesEmu AU/VST3/Standalone

`plugin/` (CMake, JUCE 8.0.4 fetched and pinned). Installed by the build to
`~/Library/Audio/Plug-Ins/{Components,VST3}/CyclesEmu.*`.

- **Engine state** is loaded at run time from
  `~/Library/Application Support/CyclesEmu/state/` (`regions.bin`, `cpu.bin`
  from `aot/dump_state.py`, 49 MB, a stopped Model:Cycles with the factory
  kit). Never bundled. Missing state = silence.
- **Per instance**: runtime and engine are instance objects now (`mc_ctx`
  with its own 4 GB reservation, peripheral log, eDMA buffer; `mc_engine`),
  so DAW instances on parallel threads are independent. Nothing allocates or
  faults on the audio thread (eDMA buffer and scratch pages touched at load).
- **MIDI**: channels 1-6 = tracks 1-6: note on/off (trig flags 0x0681 for
  every track) and the CC table with the measured clamps; FX CCs on any of
  those channels.
- **Host parameters**: per track Machine, Pitch (40-88), Decay, Color, Shape,
  Sweep, Contour, Punch, Gate, Volume+Dist, Pan, Delay/Reverb Send, the LFO
  set, Level, Mute; FX Delay Time/Feedback, Reverb Size/Tone. Initialised
  from the loaded kit; applied at block boundaries only when the parameter
  itself moved (a CC does not get undone). Generic JUCE editor.
- **Timing**: 32-frame engine blocks through a FIFO, events at the next
  boundary (<= 0.67 ms late). Other host rates: the 48 kHz stream through
  `juce::WindowedSincInterpolator`, events at the start of each host block,
  latency reported.
- **Checks** (`plugin/Test/HostTest.cpp`, `CyclesEmuHostTest`): hosts the
  built VST3. Identity: at 48 kHz the plugin's output equals the engine's
  (`aot/engine.c`, same notes) on all 48,000 frames. Load (notes on all six
  tracks every 0.5 s, a CC sweep, 128-sample callbacks): **48 kHz mean 263 us
  per callback (9.9 % of 2667 us), p99 322 us, max 0.88 ms; 44.1 kHz mean
  354 us (12.2 %), p99 439 us.** `auval -v aumu Cyem Pers`: PASS.
- Build: `aot/build.sh` once (regenerates `aot/build/render.c`), then
  `cmake -S plugin -B plugin/build -G Ninja -DCMAKE_BUILD_TYPE=Release &&
  cmake --build plugin/build`. The render object takes ~4 min the first time.

Not yet: a custom editor (and showing a missing-state error in it), saving
engine state beyond the parameters (sound names, LFO destination), the
CC 105 LFO destination, MIDI clock/tempo for LFO and delay sync, per-track
outputs, the Voice/FX split.

## Next steps

0. (done, v0.1) CyclesEmu plugin. Next: try it in a DAW; tempo/clock;
   custom editor; then the per-track split for "Cycles voice" and
   "Cycles FX".
1. Sweep every CC over 0-127 (and note velocity 0, LFO destination) with
   `ccmap.py` so the clamps in `engine_inputs.py` are complete; read the
   pattern's per-track trig flags for all six tracks.
2. ColdFire AOT for the render's code, validated with replay.py and
   driven.py; target well under 0.667 ms per block.
3. Find the per-track split inside `FUN_4005979e` for the voice/FX plugins.
