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

## Step 7: first DAW report, fixed (2026-10-21)

Jack tried v0.1 in Ableton Live: only MIDI channel 1 played, the level was
quiet, and a low-frequency build-up appeared while idle.

- **Channels.** The plugin did route channels 2-6 (the host test plays all
  six), but a Live MIDI track sends channel 1 to its instrument. New
  parameter **MIDI Mode**, default **Drum Map**: on any channel C1-F1 (36-41)
  trigger tracks 1-6 at their own pitch, notes from C3 (48) up play the
  **Keys Track** chromatically. "Ch 1-6 = Tracks" keeps the hardware's
  multi-channel behaviour. Measured: note 60 is C4 at the programmed pitch
  (Tone at pitch 64: notes 48/60/72 -> 132/264/528 Hz), so the drum map
  sends note 60.
- **Low-frequency build-up.** The firmware's output (bit-exact with digiemu)
  carries a DC ramp: the idle mean falls ~0.0002 of full scale per minute
  without levelling (-0.002 after 9 minutes), plus a ~5 Hz component around
  -64 dB. The hardware's AC-coupled outputs remove DC, so the plugin now
  runs a first-order 5 Hz high-pass at 48 kHz (`plugin/Source/DcBlock.h`).
  Idle after it: mean 0.000000, rms -62 dBFS (the firmware's own floor).
  Possibly the ramp is an emulation artefact (a rounding bias integrating
  in a filter); only the hardware could tell.
- **Level.** Engine levels are those of the device at full volume: one note
  at velocity 127 peaks -4 to -13 dBFS by track (factory kit). New
  **Output Gain** parameter, -24..+24 dB, default 0.
- **(Fixed in step 9)** an idle hum remained after the DC
  blocker: an analyser shows energy around 20-80 Hz (peak near 30 Hz) and
  thin lines near 110 and 260 Hz. It matches the firmware's own idle floor
  (-62 dBFS here: energy below 60 Hz and a decaying ~258 Hz tone). To do:
  find which track or effect makes it (mute tracks one by one, sends off).
- The host test now checks identity in both MIDI modes (through the same
  DC blocker) and the idle mean.

## Step 8: tempo sync (2026-10-21)

- **How tempo reaches the engine.** Tempo is in units of BPM x 120
  (30-300 BPM = 3600-36000). The setter `FUN_40058bb4(t)` clamps it, stores
  it at `0x40140a84` and, when `0x40140a88` is set, calls
  `FUN_40091780(0, t, 0)`, which (if the clock source is internal) stores it
  in **`0x40149310`**: the render's musical clock advances by twice this
  every 32-frame block (`0x8000184c`; 1 MIDI clock = 900,000 units, a
  quarter note 21,600,000). The clock source is the highest set bit of
  `0x40149314` (bit 0 internal, bit 1 external, `FUN_40091770`). The render
  itself applies tempo from type-2 sequencer events.
- The dumped state follows external MIDI clock (the TEMPO page shows EXT);
  its clock word read 119.55 BPM, and in a live digiemu run with no clock
  arriving it sat at 36000 (300 BPM).
- `mc_engine_set_tempo(g, bpm)`: internal source, then the setter's writes.
  Measured: delay time 16/32/64 echo after 0.53/1.03/2.03 beats at 60, 90
  and 120 BPM alike (the delay is tempo-synced; the extra 0.03 beat is the
  onset threshold and the block).
- Plugin: the host's BPM each call (applied at the next engine block), else
  the **Tempo** parameter (default 120). Host test: through the plugin with
  a fixed play head, delay time 64 echoes after 2.032 beats at 120 and at
  60 BPM. Not yet: following the host's beat position (LFO phase, retrig).
- Tools: `scripts/tempowatch.py` (panel tempo), `scripts/clockwatch.py`
  (writes to render inputs while MIDI clock runs).

## Step 9: the idle hum was a frozen reverb (2026-10-21)

- Idle, the hum (20-80 Hz, lines near 110/260 Hz, -62 dBFS) did not change
  with all tracks muted, all sends off or delay feedback 0, and vanished
  with **reverb size 0**: the reverb's own output.
- **Reverb size 127 never decays** (as emulated): a Kick into full send
  holds at -21 dB indefinitely; at size 120 the same tail is gone after
  ~25 s, at 100 after ~10 s, at 20-64 after a few seconds. With size 127 and
  **tone 64 it grows** to about -4 dB. The factory kit in the saved state
  has reverb size 127, so the reverb held the tail of the pattern that was
  playing when the state was captured. **To check on the hardware:** does
  the Model:Cycles' reverb at size 127 freeze, and does tone 64 make it
  grow? If not, the emulated EMAC arithmetic is suspect (the DC drift of
  step 7 may be the same cause).
- The reverb's state is `0x4a300000-0x4a3ecfff` (the output ring follows
  at `0x4a3ed080`); the delay lines are the 4 MB eDMA ring `0x4a400000`.
  Clearing the reverb region drops the idle floor from -57.5 to -98.6 dB and
  changes a dry note by at most one LSB. `mc_engine_new` now clears both, so
  the engine starts with silent effects; the plugin's idle floor is
  -107.6 dBFS.
- MIDI notes hold until their note-off (the firmware's MIDI request has
  length -1): in tests, send note-offs, or every machine sounds forever.

## Step 10 (open): reverb size 127 holds in the emulation, decays on the hardware

Jack (2026-10-21): on the hardware, reverb size 127 "still decays
eventually". As emulated (digiemu and the AOT agree bit for bit) it does
not. Measured, Kick into full reverb send, note-off after 0.1 s:

| size | coefficient (0x800011bc) | tone 0: dB/s (3-15 s) | tone 64 |
|---|---|---|---|
| 100 | 0.967987 | -5.69 | -7.34 |
| 110 | 0.976959 | -5.93 | -7.46 |
| 120 | 0.985901 | -3.89 | -3.90 |
| 124 | 0.989502 | -1.88 | -1.88 |
| 126 | 0.991302 | -1.30 | -1.28 |
| 127 | 0.992188 | **+0.03** (holds at -21 dB) | **grows** to -3.3 dB |

- The size word (FX+0x16 -> SRAM `0x80001750`, 127 = 0x7f000000) becomes
  the feedback coefficient at `0x800011bc` (0x7f000000 = 0.992 at 127),
  glided per block by `FUN_40058474` (one-pole, 16-bit MACs, x0x7c29 +
  x0x03d7). The reverb code reads it at `0x400579d0`/`0x40057a1a`
  (`FUN_400579c4`, which Ghidra's EMAC language cannot fully decode).
- Fitting slope = k ln(c G) to sizes 124/126 gives G ~ 1.0047 and predicts
  about -1.0 dB/s at 127; the engine shows +0.03. So either the loop has a
  size-127 special case (none found: the 0x7f00 compares in the code are
  parameter clamps) or the arithmetic gains slightly where the chip loses.
- Ruled out: MAC scale factors (none of the 394 MAC instructions use one),
  masked MAC loads (none). The render runs in fractional mode with
  truncation (MACSR 0x0a0 / 0x020) and signed integer (0x080 / 0x000).
- Translator note: `ext()` returns long-size expressions unparenthesised;
  correct today only because `+`/`-` bind tighter than `^`/`&`. Parenthesise.
- **Interim (Jack agreed):** the plugin plays reverb size 127 as 126, at
  load (the factory kit has 127) and from the parameter or CC 87.
- **Needed:** a hardware reference. A recording of one Kick hit (track at
  full reverb send, size 127, tone 0, and the same at size 120) would give
  the true decay rates to compare.

## Step 11: LFOs as on the hardware, and the editor (2026-10-21)

LFO facts, from the firmware (`scripts/ccsweep.py`,
`scripts/lfodest_screens.py`, `scripts/lfopage_values.py`, measurements):

- **Destination** = record word `+0x1c` (CC 105). The firmware accepts only
  0 and 10-22, the index of the target parameter in the record (index =
  (offset - 0x14) / 2): 0 `---`, 10 PTCH, 11 COLR, 12 SHPE, 13 SWEP,
  14 CONT, 15 PAW (Punch), 16 GATE, 17 FTUN (fine tune, record `+0x36`, no
  CC of its own), 18 DEC, 19 DIST, 20 DELS, 21 REVS, 22 PAN. Other values
  are ignored (why 37 and 90 wrote nothing in step 4).
- **Multiplier** = `+0x18` (CC 103), 0-23: 0-11 show as x1..x2k and are
  **tempo-synced**, 12-23 show as 1..2k and **run free** at the 120 BPM
  rate. Measured with the LFO on pan, speed +32: x4 cycles at 0.23 Hz at
  60 BPM and 0.50 Hz at 120; plain 4 at 0.50 Hz at both.
- **Waveform** `+0x1e` 0-6: triangle, sine, square, saw, ENV, ramp, random
  (as drawn on the LFO page). **Speed** `+0x16` and **depth** `+0x24` are
  bipolar (64 = 0; speed 64 stops the LFO); fade `+0x1a` bipolar as on other
  Elektron machines. **Trig mode** `+0x22` 0-4: drawn as icons, so the names
  Free/Trig/Hold/One/Half are Elektron's usual order, **unverified**.

Plugin:
- Parameters per track: LFO Sync (Sync/Free) + LFO Multiplier (1..2k)
  combine into the firmware's 0-23; LFO Destination (14 choices); LFO
  Waveform; LFO Trig Mode; Speed/Depth/Fade shown -64..+63; Pitch shown in
  semitones; Pan as L/C/R. CC 103 and CC 105 pass the firmware's raw values
  (invalid destinations ignored, as the firmware does).
- **Follow Host Tempo** (default on): off, the Tempo parameter rules.
- **Editor** (`Source/PluginEditor.*`): global strip (MIDI mode, keys track,
  tempo follow, tempo, output), a page per track (SOUND: machine, punch,
  gate, mute, the six machine knobs, vol+dist, pan, sends, level; LFO:
  waveform, trig mode, sync, multiplier, destination, speed, depth, fade,
  phase) and an FX page. `CyclesEmuEditorShot` renders every page to PNG
  (`notes/editor-*.png`) to check the layout without a host.
- Engine status (or the missing-state error) shows top right.

## Step 12: the retro editor (2026-10-22)

Restyled after Fors, Windows 95 and PS1/N64 games. The look is in
`plugin/Source/Retro.h/.cpp`, a LookAndFeel plus drawing helpers. Every
control and parameter binding is the same as before.
- Win95 chrome: a navy title bar dithered to blue (4x4 Bayer, 2 px cells),
  3D bevels, etched group boxes, tabs, white sunken dropdowns, checkboxes,
  and a status bar.
- Type: a 5x7 bitmap font drawn in code at 2 px per pixel, upper case only,
  so no font files are needed. Labels are sized to fit it: "Vol+Dst",
  "Key Trk", "Host Tempo", and the MIDI mode choice "Drum Map C1-F1".
  Choices are saved by index, so renaming one doesn't break saved sessions.
- Knobs: flat-shaded low-poly octagons inside a ring of 15 pixel ticks.
  Bipolar knobs light from the centre. Each value readout is a black LCD
  with green text.
- Colour: each page has one accent, the "accent" property on its page,
  read by `retro::accentFor`. Tracks 1-4 use the N64 red, yellow, green
  and blue, then purple and red; FX is grey.
- Section rows with no knobs are short (52 px). Knob rows share the
  remaining height.
- Host tests and auval pass unchanged.

## Step 13: the "?" help mode (2026-10-22)

A "?" caption button in the title bar, as on Win95 dialogs, toggles help
mode. While it is on, hovering over any control shows a yellow tooltip box
(`HelpBubble`) with the control's name and what it does. The status bar
says how to leave the mode.
- The text lives in `plugin/Source/Help.cpp`, keyed by the bare parameter
  id, so the Voice and FX plugins can share it.
- Color, Shape, Sweep and Contour read the track's machine and describe
  that machine's version.
- **The per-machine macro lines are approximate.** The manual says only that
  each knob is a macro over several FM settings, and that Metal's Color
  detunes its oscillators. Correct them by ear or against the hardware.
  The LFO trig mode names are still unverified (step 11).
- Hovering is caught with `addMouseListener(this, true)` on the editor.
  `showHelpFor(id)` does the same for the PNG renderer, which writes
  `notes/editor-help-*.png`.

## Step 14: Fors-style symbols and the LFO display (2026-10-22)

Ideas taken from Fors' plugins:
- Tela gives every parameter a pictogram (a dot grid for Gap, nested
  squares for Damp).
- Pivot draws each operator's waveform and envelope as a small graph.
- Opal has a large waveform display and a column of machine icons.

What changed here, kept to the Win95/PS1 look:
- **Pictogram buttons** (`Icons.cpp`, 9x9 pixel icons; `IconButton`,
  `ParamControl::Style::Icons`), replacing four dropdowns:
  - Machine: Kick rings, Snare noise, Metal nested squares, Perc diamond,
    Tone sine, Chord stacked bars.
  - LFO waveform: drawn from the same functions as the display.
  - Trig mode: a bar marks the note, then free, restart, hold, one cycle
    or half a cycle.
  - Sync/Free: a crotchet, or an infinity loop.
  A selected icon is a sunken LCD with green pixels, like the value
  readouts.
- **LFO display** (`LfoScope`): two cycles from the note.
  - The LFO as it plays (depth and its sign, start phase, fade, direction
    from the sign of speed, trig mode) over a faint trace of the plain
    waveform.
  - Header: the waveform, the cycle length (bars or beats when synced,
    Hz when free or very fast) and the destination.
  - A white dot marks the estimated position. It is computed from engine
    time since the track's last note-on (`secondsSinceTrig`, two relaxed
    atomics in the audio thread), or since start in Free mode. It is not
    read from the firmware.
  - It checks at 30 Hz while visible and repaints only on change. With no
    host running, it stays idle.
  - Rate is exact (step 11's measurement): speed x multiplier / 128
    cycles a bar.
  - **Approximate:** the fade time scale is drawn as |fade|/64 of the
    window, and the Saw direction and the Env, Ramp and Random shapes
    (`LfoShape.h`) follow other Elektron machines.
- `notes/editor-lfo-demo.png` sets every LFO setting and renders the result.

## Next steps

0. (done, v0.1) CyclesEmu plugin. Agreed order (Jack, 2026-10-21):
   a. (done, step 9) the idle hum: the reverb at size 127 froze the
      captured tail; effects now start silent;
   b. (done, step 11) a proper editor, with
      - **a button: follow the host tempo, or not** (then the Tempo
        parameter rules even inside a DAW);
      - **each track's LFO synced to tempo or free running, as the
        firmware offers.** The LFO Multiplier takes 0-23 (measured clamp),
        likely 12 synced multipliers (x1..x2k) and 12 free-running ones
        (.1...2k, fixed rate) as on other Elektron machines: verify the
        mapping by measuring LFO rate against tempo, then expose it as a
        Sync/Free switch plus a multiplier;
      - **each track's LFO destination**, as on the hardware (CC 105
        wrote nothing to the kit when mapped: find where it lives);
   c. the per-track split for "Cycles voice" and "Cycles FX".
   d. (done, step 13) the "?" help button.
1. Sweep every CC over 0-127 (and note velocity 0, LFO destination) with
   `ccmap.py` so the clamps in `engine_inputs.py` are complete; read the
   pattern's per-track trig flags for all six tracks.
2. ColdFire AOT for the render's code, validated with replay.py and
   driven.py; target well under 0.667 ms per block.
3. Find the per-track split inside `FUN_4005979e` for the voice/FX plugins.
