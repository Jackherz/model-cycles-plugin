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
  - (Superseded in step 16: a white dot marked the LFO's estimated
    position, which vanished at high rates. The window is now one bar, and
    the number of cycles drawn grows with the speed.)
  - It checks at 30 Hz while visible and repaints only on change.
  - Rate is exact (step 11's measurement): speed x multiplier / 128
    cycles a bar.
  - **Approximate:** the fade time scale is drawn as |fade|/64 of the
    window, and the Saw direction and the Env, Ramp and Random shapes
    (`LfoShape.h`) follow other Elektron machines.
- `notes/editor-lfo-demo.png` sets every LFO setting and renders the result.

## Step 15: optimisation (2026-10-22), -36% engine, -38% / -48% plugin CPU

Before -> after (M1 Pro). Engine: mean per 32-frame block, `aot/build/bench`.
Plugin: CyclesEmuHostTest, 128-sample callbacks.

| | before | after |
|---|---|---|
| engine, worst case (6 Chords, decay and sends 127) | 71.0 us | 45.8 us |
| engine, every machine in turn (`BENCH_MODE=mix`) | 70.9 us | 46.0 us |
| engine, the kit as loaded (`BENCH_MODE=kit`) | 64.5 us | 42.3 us |
| plugin at 48 kHz | 273 us (10.2 %) | 168 us (6.3 %) |
| plugin at 44.1 kHz | 368 us (12.7 %) | 191 us (6.6 %) |

Where the time was:
- A counting build (render.c with `-DMC_TRACE` and a per-PC counter)
  shows 82k instructions per worst-case block.
- 40 % are EMAC: mac 24 %, from_mac 12 %, to_mac 3 %.
- 39.5 % of all instructions are one 41-instruction loop at `0x400a8942`:
  the per-voice oscillator, 32 samples x 24 calls a block
  (6 tracks x 4 Chord voices).

What changed:
1. **Lazy MACSR flags** (`translate.py`, `runtime.h` `mc_mac_nf` /
   `mc_mac_flags`). MACSR lives in three locals:
   - control bits (change only on `move to MACSR`);
   - the sticky PAV bits;
   - the N/Z/V/EV flags, computed only when MACSR is read
     (`from_macsr`, `macsr_to_ccr`, exit).
   A MAC op records its result (`MAC_LAZY`); clang deletes those stores
   when nothing reads them. The mode check now depends only on the control
   bits, so it is loop-invariant. Alone: -19 %.
2. **Profile-guided build**, about -20 % on top:
   - `aot/pgo.sh` trains an instrumented bench on the worst case, every
     machine (`BENCH_MODE=mix`: machines, macros, LFO and sends rotating)
     and the kit, and writes `aot/build/render.profdata`.
   - The plugin's CMake uses that file when it exists.
   - Rerun pgo.sh after translate.py. A stale profile is ignored with a
     warning: slower, not wrong.
   - Tried and dropped: `-O3` (-4 %, about noise), `-mcpu=apple-m1` (0).
3. **Parameter polling**: each block reads the parameters' raw atomics
   instead of `getValue` + `convertFrom0to1` (148 virtual calls).
4. **Resampler** (`Source/Resampler.h`), replacing JUCE's
   WindowedSincInterpolator:
   - Polyphase windowed sinc: 192 taps, Kaiser beta 12.5, 1024 phases with
     linear interpolation between them, vector dot products, both channels
     sharing one table.
   - Cutoff from the lower rate. JUCE's did not low-pass when going down to
     44.1 kHz, so 22.05-24 kHz content aliased at full level.
   - `CyclesEmuResamplerTest`, ours against JUCE's:

     | | JUCE | ours |
     |---|---|---|
     | error, 48 -> 44.1 kHz | -109 to -92 dB | -144 to -131 dB |
     | error, 48 -> 96 kHz | -133 to -112 dB | -144 to -137 dB |
     | alias of a 23.5 kHz tone | -6 dBFS | -139 dBFS |
     | time per 128 outputs | 35 us | 3-5 us |

   - JUCE's also lost 0.09 dB of level.
   - Trade-off: the passband is flat to about 19.5 kHz, with the
     transition band below 22.05 kHz.

Checks, all passing:
- `check_replay.py`: base 300/300, chord 100/100, kick 300/300.
- `check_driven.py --keep-going`: 197/200. The same three note-on RTOS-page
  differences as in step 5.
- HostTest: identity, idle and tempo as before, plus a new test, the
  **resampled path**: a 44.1 kHz render against a continuous
  double-precision evaluation of the same kernel, residual -142.7 dB.
- auval passes.

## Step 16: the Syntakt's digital engines (2026-10-22), route A

Machines 7-11 are now **SD Vintage, CP Vintage, SY Toy, SY Bits and SY
Swarm**: the Syntakt's own engine code, inside Model:Cycles 1.13 by
Modded-Cycles' tweak `syntakt-sd-cp-toy-bits-swarm`.

What it is: the tweak copies the five engines' code and tables (and a
voice bridge with a load governor) from the user's `Syntakt_OS1.42.syx`
into a 269 KB payload. Boot copies it to `0x43000000`, then the code and
three tables into internal SRAM (`0x8000xxxx`, where it runs). The tweak
makes 124 patches to the MAIN OS: machine limits, the voice loop detour
at `0x400a7dfe`, a probe around the audio call, and others.

### The pipeline, all local

1. **Build** (Jack's two files, `re/modded-cycles` cloned 2026-10-22 at
   2594419):
   `python3 re/modded-cycles/tools/build.py -i firmware/model-cycles_OS1.13.syx --syntakt firmware/Syntakt_OS1.42.syx -t syntakt-sd-cp-toy-bits-swarm -o data-st/model-cycles_OS1.13_syntakt5.syx`
   It patches 269,894 bytes; patched MAIN OS sha256 `c6cdfe22…7c`.
2. **Boot** (`scripts/boot_firmware.py data-st SYX`): digiemu's own first
   run (extract, format a card, cold boot, intro, settle). Settled at
   600M instructions. `data-st/devices/` overlays digiemu's device file
   with this build's sha256.
3. **Tools on it**: `CYCLES_FW=data-st:model-cycles_OS1.13_syntakt5.syx`
   (`scripts/emuhost.py`). `CYCLES_MACHINES=11` gives `engine_inputs.py`
   the firmware's clamp: CC 70 = 127 selects machine 10.
4. **Coverage**: `scripts/coverage_batch.sh` with `MACHINES="0 … 10"`, plus
   scenarios for the knobs the Syntakt engines map differently. 1,530
   blocks (stock: 991).
5. **State**: `capture.py` (no `--stop`: this OS boots stopped, and two STOP
   presses started it), then `dump_state.py` -> `data-st/aot-state`.
   Installed as `~/Library/Application Support/CyclesEmu/state`; the stock
   one is kept as `state-stock`, the modded one as `state-syntakt5`.
6. **Translate**:
   `translate.py --sections ../data-st/sections --coverage ../data-st/coverage.json --state ../data-st/aot-state --code 43000000:43042000 --code 80000000:80010000 --out build-st/render.c`.
   The translator now also reads code outside the image, from the state's
   RAM. 15,912 instructions (stock 11,353); no new unsupported opcodes.
7. **Plugin**: CMake uses `aot/build-st` when present (`-DMC_BUILD=build`
   for stock). Run `aot/pgo.sh STATE build-st`, which trains on all 11
   machines.

### Behaviour, and how it was checked

- **Three new peripheral reads**: eDMA channel 30's CSR (DONE), INTC0's
  force register, and **DMA timer 0's counter, the governor's clock**.
  - `engine.c` holds the timer still, so every block measures zero load.
    The governor never fades voices to save CPU time (on hardware it does
    above 82 %).
  - At low load it still stops computing a voice that has stayed below
    -108 dB for 64 blocks with no trig (inaudible), as on hardware.
- **This OS runs a clock interrupt** (`0x40054c1e`, armed by `0x400559f0`)
  that queues a type-5 event every ~31 blocks. The render frees type-5
  events unread (jump table `0x40058da4`: types 3 and 5 share the "free"
  case). The plugin's engine never runs that interrupt: no events, as in
  stock.
- **Unicorn, driven** (`scripts/driven.py --synth --between`, which
  re-creates the clock events and SRAM writes; `Replay.by_address`):
  195/200. The rest is the UI post: the RTOS pages, and `0xFC050014 |=
  0x2000` on four blocks.
- **AOT, replay** (`check_replay.py --ignore 43032000:43033000`, all five
  Syntakt engines plus Kick): 300/300 bit-exact outside the bridge's
  bookkeeping page.
- **AOT, driven from MIDI like the plugin** (`check_driven.py --between
  --ignore …`): 195/200, the same UI-post signature. The governor ran on
  the frozen clock throughout without a sample of sound changing.

### Plugin

- Machine 0-10. Five new pixel icons: snare drum, clap burst, toy robot,
  bit staircase, swarm of saws.
- On the Syntakt machines the knobs relabel to the engine's own parameters
  (Modded-Cycles' guide). The help text names them in full.
- **LFO display**: the dot is gone. The window is one bar (of the tempo
  when synced, of 120 BPM when free). The faster the LFO, the more cycles,
  growing continuously from 1 to 64; each column draws the curve's range,
  so dense LFOs read as a band.
- The fresh-boot kit replaces the factory kit as the plugin's starting
  sounds.

## Step 17: a filter page per track, and the Codec look (2026-10-22)

### The filter: Modded-Cycles' `track-filter` (their notes/47)

- A 2-pole trapezoidal SVF (12 dB/oct) after Vol+Dist, before pan, level
  and sends, as on the Model:Samples. Bit-transparent at OFF.
- Four real per-track parameters, k 28-31, at sound-record offsets
  `0x4c`/`0x4e`/`0x50`/`0x52`:
  - Cutoff L64..OFF..H63: low-pass 5 Hz-20 kHz, high-pass 5 Hz-19 kHz.
    CC 74.
  - Reso, Q 0.5-50. CC 71.
  - Env: a bipolar amount, restarted by each note.
  - Env Decay, 5 ms-10 s.
  - All four are LFO destinations 28-31.
- Their cost figure: at most ~5,100 instructions per block, all six tracks
  filtered and modulated.

The build is the step 16 pipeline, now one script:
- `data-stf/`, from
  `build.py … -t syntakt-sd-cp-toy-bits-swarm,track-filter`. Patched MAIN
  OS sha256 `a280f7ca…b5ac`, .syx `75a0243e…49a0`.
- `scripts/firmware_pipeline.sh data-stf SYX "0 … 10" FILTER=1` runs boot,
  coverage, capture and state dump. The coverage batch's `FILTER=1` adds
  cutoff x reso and per-machine filter scenarios.
- Translation to `aot/build-stf/render.c`: 16,178 instructions, 1,577
  coverage roots, no new unsupported opcodes.
- CMake prefers `build-stf`, then `build-st`, then `build`.
- The installed state is `state-syntakt5-filter`; `state-syntakt5` and
  `state-stock` are kept.

Checks:
- Unicorn driven (`driven.py --synth --between`, the capture now also sends
  CC 74 and 71): 195/200, the same UI-post signature as step 16.
- AOT replay (`cap-filter`: all six tracks filtered, LP and HP, reso 100,
  stock and Syntakt machines): 300/300 bit-exact outside the governor's
  page.
- AOT driven: 195/200, the same signature.

Plugin:
- Parameters `fcut`/`freso`/`fenv`/`fdec`. Cutoff shows L/OFF/H, Env shows
  ±. CC 74/71 route through the CC table. CC 105 also accepts 28-31; the
  LFO destination list has 18 entries.
- Each track page gains a full-width **FILTER** section (the window is
  960x760):
  - the four knobs;
  - `FilterScope`, a Codec screen of the response: log frequency, LP or HP
    magnitude with the resonance peak, flat at OFF, cutoff mark, envelope
    in the header.
  - The scope's cutoff law (about 2 semitones a step) and Q law are
    approximate; the exact table is in the tweak's code.
- `bench.c`'s mix mode moves the filter too (`BENCH_FILTER`, set by
  `pgo.sh` for `build-stf`).

CPU (HostTest, 48 kHz / 128, profile-guided build): 6.5 % with the
filters off. 6.8 % with all six tracks through a resonant low-pass (the
load test now sends CC 74/71 at the start). auval and all five host tests
pass.

### The Codec look (Metal Gear Solid, PS1)

- `Retro`:
  - the Codec palette (phosphor `#8dffb2` on `#040d07`, gunmetal bevels,
    grey plates);
  - `codecScreen`, `scanlines`, `labelPlate`, `sevenSeg`, `signalBars`.
  - The "LCD" colours now are the Codec's: readouts, selected icons and
    both scopes are scanlined Codec screens.
- **CodecTempo** replaces the tempo knob and the Host Tempo checkbox:
  - a TEMPO plate, and a screen with signal bars (the output peak, -48..0
    dB, from `outputPeak()`, a relaxed atomic set once per callback);
  - a seven-segment BPM readout with ghost segments, and ◀ ▶ (±1 BPM, shift
    ±0.1);
  - drag the screen (1 BPM per 2 px, shift finer);
  - a HOST plate, lit while following the host; click to toggle.
- LFO scope HUD: "- TRI - 1 BAR -" title, a rule over the plot, corner
  brackets, and the destination as a boxed tag on the plot (like the
  scope's "FLY").
- The help lookup now works by a `helpId` component property, so the Codec
  unit and the filter scope have hover help too.

## Step 18: track presets, kits, randomisation (2026-10-22)

- **Menu bar** (Win95, under the title bar; the window is 960x780):
  - Kit: Load Kit, Save Kit As, Randomise All Tracks (Sound).
  - Track (the current track page): Load, Save As, Copy, Paste, Randomise
    and Vary for Sound, LFO and Filter.
  - Help: help mode, Show Presets Folder.
- **Files** (`Source/Presets.*`): XML in `~/Documents/CyclesEmu/Tracks/*.cytrack`
  and `Kits/*.cykit`, with plain values by bare parameter id, so files
  survive added parameters.
  - A track preset is the sound, LFO and filter: everything but level and
    mute, which belong to the mix.
  - A kit is all six tracks (with level and mute) and the FX.
- **Randomise** (`Source/Randomise.*`): a die on each section's title line;
  shift-click (or Vary in the menu) is a variation that nudges every
  0..127 knob by ±10. Machine, level and mute are never touched.
  Distributions:
  - pitch: 65 % as programmed, else ±3/5/7/12 semitones;
  - decay: around 30/60/90;
  - macros: half uniform, half near 0/32/64/96/127;
  - Vol+Dist: around 64; pan: mostly centre;
  - sends: about half off;
  - LFO: any waveform, mostly Trig or Free, mostly synced, multiplier
    x2..x64, speed ±8..63 (85 % forward); destination from pitch, the
    macros, decay, pan and filter cutoff/reso; depth ±2..8 on pitch, else
    ±10..45; fade mostly off;
  - filter: 30 % off, else a low-pass L4..L30 or a high-pass H2..H20,
    moderate reso, the envelope half the time.
- Checked in `CyclesEmuEditorShot`:
  - a randomised track 1 saved to a file and loaded into track 4: 0 of 26
    parameters differ, track 4's level kept;
  - kit round trip: 0 differences;
  - 2,000 draws: filter off 29 % / LP 45 % / HP 25 %, pitch at 0 65 %,
    LFO multipliers even.

**SP TWINSHOT** (asked 2026-10-22):
- Not portable as the Syntakt plays it (Modded-Cycles notes/24). The
  engine code is in the Syntakt OS (5 functions, cheap), but it plays two
  of 64 factory samples, which the Syntakt's main CPU sends to the synth
  chip at boot from its own storage. They are not in the OS file.
- Possible instead: its engine with **your own samples** loaded as the
  64-slot bank. That is a new engine port, like Modded-Cycles' five but
  done by us: relocation, the voice bridge and the parameters. Here its
  CPU cost would be small.

## Step 19: typing values in the retro style (2026-10-22)

`retro::PixelEntry`, the sliders' value readouts (`Look::createSliderTextBox`):
- Clicking one turns it into a lit Codec prompt that echoes the typing in
  the pixel font, with a blinking block cursor. No system text box.
- It accepts what the readouts show: 0-9 + - . and L R C H O F.
- Return enters the value through the slider's own text parsing (so "L16",
  "+12", "OFF" work), Escape cancels, clicking away enters.
- Gotcha: sliders build their readout when constructed, before they meet
  the editor's look. The editor's constructor ends with
  `sendLookAndFeelChange()`, so they rebuild it as a PixelEntry.
- Checked in EditorShot: typing 100 into track 1's Decay sets it to 100
  (`notes/editor-typing.png` is mid-entry).

## Step 20: Reset Track, Reset Kit, Undo (2026-10-22)

- **Reset Track** (Track menu): the track's sound back to its machine's
  **init sound, as the firmware makes it**. Level goes to the new kit's,
  mute off.
  - The values come from the firmware's init-sound function `0x40061866`
    (sound, unused, machine), which writes each k 0..32 from the
    descriptors' defaults.
  - `scripts/init_sounds.py CAPTURE 11 OUT` runs it in Unicorn for every
    machine and writes `defaults.json`. That file is firmware-derived: it
    goes in the engine state folder (now part of
    `scripts/firmware_pipeline.sh`), never in the repository.
  - Without the file, the menu item is greyed.
- **Reset Kit** (Kit menu): back to the kit a new project starts with. The
  engine state is a freshly booted new project, so the processor snapshots
  its kit (all tracks, levels, mutes, FX) as it loads.
- **Undo** (Kit menu): one step, covering Reset, Load, Paste, Randomise,
  Vary and the dice. It restores the whole kit as it was; choosing it again
  redoes.
- Checked in EditorShot:
  - a randomised track 2 reset: 0 of 23 sound parameters differ from
    `defaults.json`;
  - a randomised kit reset: identical to the new kit.

## Step 21: colour schemes (2026-10-22)

**View** menu: five schemes, each chosen by its background, with every
other colour offset to suit it (`retro::applyTheme`, `kThemes` in
`Retro.cpp`):
- the bevels' light, highlight and shadow from the face;
- readable text and fields;
- a title bar and selection colour that sit with the face;
- Codec screens in a complementary phosphor, with matching gunmetal and
  plates.

| Scheme | Face | Title | Screens |
|---|---|---|---|
| Grey | Win95 standard | navy | green |
| Desert | sand | teal | amber |
| Lilac | lavender | violet | cyan |
| Slate | blue-grey | deep blue | lime |
| Midnight | dark, with light text and dark fields | indigo | pink |

- The tracks' N64 colours and the yellow help bubble (dark text) stay the
  same in every scheme.
- The palette is shared `inline` colours read at paint time, so a change
  repaints everything.
- The choice is a preference for all instances, not part of a session:
  `~/Library/Application Support/CyclesEmu/CyclesEmu.settings`, applied
  before the editor first paints.
- Plain labels now take the scheme's text colour at paint time, not the
  look's colour from startup.
- `notes/editor-colour-*.png`: one render per scheme.

## Step 22: knob values readable in every scheme (2026-10-23)

Jack's report: track 2's yellow ticks vanished on Grey (contrast 1.0:1).

- **Lit ticks**: the track's colour passed through `retro::readableOn(c,
  kFace, 2.0)`. It is darkened on light faces, lightened on dark ones,
  until its WCAG contrast with the face reaches 2:1. A 2:1 target keeps the
  hue: a 3:1 yellow on grey is brown.
- Each lit tick also gets a **1-px ink outline**: black where the face
  reaches 7:1 against black, else white (Midnight). The outline is what
  guarantees visibility.
- **Unlit ticks**: small 2x2 shadow dots, so lit and unlit differ by shape
  as well as colour.
- `CyclesEmuEditorShot` prints raw -> drawn contrast for every scheme x
  track colour. All end >= 2.0 before the outline; the worst raw case was
  yellow on Grey, 1.0. It renders `notes/editor-knobs-<scheme>.png`.

## Step 23: CyclesVoice, one sound on six voices (2026-10-23)

The first half of the Voice/FX split. **CyclesVoice** (AU/VST3/Standalone,
code `Cyvo`) is built from the same sources as CyclesEmu with
`CYCLES_PRODUCT=1` (`Product::Voice` in PluginProcessor.h).

- **One sound, six voices.** The layout holds only track 1's parameters
  (the "Voice" group), the FX, and **Voices** (1-6); no MIDI Mode or Key
  Track. Every track-1 parameter, and every CC on any channel, is applied
  to all six engine tracks (`applyAll`). At start the engine's track 1 is
  copied to the other five.
- **Voice allocation** (`voiceNote`): notes on any channel, chromatic.
  - A note takes the free voice released longest ago; with none free, it
    steals the oldest playing.
  - Note-off releases the voice holding that note.
  - Voices = 1 is mono: each note cuts the last.
- **Editor**: a VOICE tab and FX; a Voices knob where MIDI Mode and Key Trk
  were; a Voice menu (Undo, Load/Save/Copy/Paste/Reset Sound, Randomise and
  Vary) instead of Kit and Track; title "CyclesVoice.exe".
  - Sound presets are the same `.cytrack` files as CyclesEmu's track
    presets.
- **Fix found on the way**: the constructor applied each slot while filling
  them. Applying LFO sync reads the multiplier's slot (crash when it was
  still null), and wrote the engine before later slots read their engine
  values. Now: all slots, then all engine values, then apply.

### Checks

- EditorShot: after start, 0 engine values differ between the six voices.
- Steady state (settings, 1 s, then notes; the 0.8 s after):
  - the same note on three voices is exactly 3.00x one note;
  - a C-E-G chord's level equals its notes' energies added (0.2181 against
    0.2182);
  - the chord at 1 voice is one note's level.
- HostTest (the built VST3) checks the same. A busy six-voice pattern costs
  8.2 % at 48 kHz / 128. auval passes for Cyem and Cyvo.
- **Gotcha**: a note within ~10-100 ms of a machine change catches the
  firmware still settling it (levels jumped 0.03-0.2 depending on the exact
  block). That is the firmware, not the voices; tests now let it settle.
- `mc_engine_kit` and `mc_engine_peek` (`engine.h`) give tests a look at
  guest memory (kit header, the per-track buffers at
  `0x80001858 + 128 t`).

Next: **CyclesFX**, the send effect. Find where the render sums the sends
(the mixer `0x400567ba`) and add a hook to the translator, so the host's
audio can be added there mid-render.

## Step 24: Mute that mutes, Solo, and a mixer page (2026-10-23)

- **Mute did nothing to the sound.** The firmware's mute
  (`FUN_400594f0`, the mask at `0x40a700c0`+0x14) only stops the
  sequencer playing a track's trigs: notes from MIDI still sound, and the
  plugin has no sequencer. The firmware matching checks passed because
  that is the firmware's behaviour.
- Mute and the new **Solo** now work as on a mixer:
  - `applyMix()` sets each track's engine level to its Level, or to 0 while
    the track is muted or another track is soloed and it is not.
  - The firmware mute mask is still set too.
  - Only changes reach the engine, and the firmware's level smoothing makes
    the switch click-free.
  - Solo is a plugin parameter per track (`Kind::Solo`), left out of track
    presets like Level and Mute.
- **MIX tab** (CyclesEmu, between Track 6 and FX): six strips. Each has the
  track's chip and name; a Codec meter of 12 segments (-48..0 dB, fast up,
  slow down); Level, Pan, Delay and Reverb; and Mute and Solo as switch
  plates (`ParamControl::Style::Switch`: grey, lit Codec when on).
  - The meter is the track's own signal, before its level: the render's
    per-track buffers at `0x80001858 + 128 t` (32 big-endian Q31 samples),
    read after every engine block (`mc_engine_read`). It dims, with "Off",
    while the track is silenced.
  - Tabs now share the bar's width (8 in CyclesEmu).
- Checked in EditorShot:
  - Mute before a note: silence. Mute while it rings: 0.0001 after
    smoothing. Mute and unmute: level back.
  - Track 2 soloed silences track 1; both soloed play it at full level.
  - A note on track 3 shows on track 3's meter only.
  - Host tests (6) and auval (Cyem, Cyvo) pass.

## Step 25: CyclesFX, hardware defaults, musical delay times (2026-10-23)

### CyclesFX: the delay and reverb as an audio effect

- **The mixer** (`0x400567ba`, every block, in order):
  1. Sums the tracks' delay sends (per-track ramped gains `0x40a78bd0` L /
     `0x40a78bb8` R, `0x40056716`) into an interleaved stereo Q31 bus,
     `0x8000b990` (L) / `0x8000b994` (R), 32 frames, 8 bytes a frame.
  2. Runs the delay, `0x40057488(0x8000ba90, 0x8000b990, ...)`, called at
     `0x4005699c`.
  3. Copies the delay's return into a seventh track slot (`0x80001b58`, by
     `0x400567a4`).
  4. Sums the reverb sends with it into the same bus (`0x40056698`, gains
     `0x40a78b9c` / `0x40a78b80`).
  5. Runs the reverb, `0x400579c4(0x8000bb90, 0x8000b990, ...)`, at
     `0x40056a2c`.
  6. The final mix follows. Profiled: every block runs all of it.
- **Translator `--hook PC`**: before that instruction, `if (X->hook)
  X->hook(X, pc)`. `mc_ctx.hook`/`hook_user` are new; a NULL hook costs one
  branch.
  - `build-stf` is translated with `--hook 4005698c --hook 40056a1c`, the
    first pushes of the delay and reverb calls. Both PCs were already
    labels, and render.c gained exactly those two lines.
  - Still 300/300 bit-exact with no hook installed (`cap-filter`).
- **Engine API**: `mc_engine_set_input(g, l, r, delaySend, reverbSend)`.
  Its hook adds 32 frames of input times each amount (floats x 2^31,
  saturated) to the bus before each effect.
- **Plugin**: `Product::Fx`, target CyclesFX (`aufx Cyfx Pers`).
  - Stereo in and out, no MIDI.
  - Parameters: the four FX, plus Delay Send / Reverb Send (0-127, square
    law; default 80) and Mix (0-100 %, default 100 = effects only, for a
    return channel).
  - Page: INPUT, DELAY and REVERB sections. `Section::minColumns` lets
    small sections use wider cells.
- **Timing**:
  - At 48 kHz each engine block takes the 32 frames that arrived during the
    previous one: latency 32.
  - At other rates the input is resampled to 48 kHz, the same blocks run
    (a block's input is its own), and the result is resampled back. Dry and
    wet go through the same path, so they stay aligned.
  - Both sides start with silence (output `kTaps + 64` at 48 kHz, input
    `kTaps + 128` at the host rate) so neither runs dry. Latency =
    inAhead - D + (outAhead - D) * rate / 48k (D = 95), 373 at 44.1 kHz.
  - A first try with only an input head start ran the input dry at start:
    silence blocks, and a shifted latency (measured 237, reported 484).
- **HostTest `fxTest`** at 48 and 44.1 kHz:
  - sends 0: silence;
  - a click into the delay alone: its echo;
  - Mix 0: the input after exactly the reported latency (cross-correlation:
    32/32, 373/373);
  - load 5-6 %.

### Hardware defaults

- `defaultFor(id)`: a track parameter's value in its current machine's
  firmware init sound (`defaults.json`); level from the new kit; mute and
  solo off; the FX from the new kit. Anything else: the declared default.
- Knobs return to it on double-click (`ParamControl::Knob`).
- The parameters report it to the host through `HardwareDefault<...>`
  wrappers, whose `getDefaultValue` asks the processor.
- EditorShot: after Reset Track, 0 of 29 parameters are off their default,
  and 0 report a different one to the host.

### Musical delay time and LFO speed

- Delay time (32 to the beat, `Show::NoteLength`) shows note lengths by
  name: 1/64, 1/32, 1/32., 1/16, 1/16., 1/8, 1/8., 1/4, 1/4., 1/2, 1/2.,
  1 BAR at 127. The names can also be typed.
- While dragged, the knob snaps to them within 1.5 steps
  (`Knob::snapValue`, `snapPoints`); Alt turns it freely.
- LFO speed snaps the same way to 64 ±4, 8, 16, 32, 48 and the ends.

## Step 26: LFO destination names follow the machine (2026-10-23)

Jack's report: the LFO page kept naming the destinations Color, Shape,
Sweep, Contour and Punch whatever the machine. On the Syntakt engines they
are other parameters: SD Vintage's Shape is Freq Complex, SY Bits' Punch is
Bit Redux.
- The track page's machine listener (the one that relabels the knobs) also
  renames those five entries of the Destination list (2-6,
  `ParamControl::setItemText`).
- The LFO display's destination tag uses the same names
  (`LfoScope::destName`).
- This works in CyclesEmu and CyclesVoice.
- The listener's first update now runs after the list exists.
- EditorShot: track 1 on SD Vintage lists "Freq Complex", and on Kick
  "Shape" again.

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
   e. **LFO display: speed as a growing number of cycles** (Jack,
      2026-10-22). The dot (step 14) disappears at high rates. Instead,
      draw the window at a time span that shows more cycles as the speed
      rises, seamlessly (continuous zoom, not steps), so the drawing itself
      shows the speed in real time.
   f. (first pass done, step 17) **UI: more Metal Gear Solid (PS1) Codec / scope influence** (Jack,
      2026-10-22, two reference screenshots in chat):
      - near-black screens with phosphor-green, scanlined panels;
      - bevelled Codec frames with small grey label plates ("PTT",
        "MEMORY");
      - segmented signal-strength bars and a large seven-segment readout
        ("140.85");
      - thin green HUD rules and labels ("ZOOM LEVEL - 100 -", "ANGLE -5"),
        left/right arrow buttons, the radar's angled green lines.
      Candidates: the LFO display and value readouts as Codec screens, a
      big frequency-style readout for tempo, signal bars for levels and
      sends, HUD-style section titles.

   g. (done, step 17) **A filter page per track** (Jack, 2026-10-22, next pass). Use
      Modded-Cycles' tested `track-filter` tweak (their notes/47):
      - What it is: a bipolar DJ filter, L64..OFF..H63, a 2-pole
        trapezoidal SVF placed after Vol+Dist. Bit-transparent at OFF.
        Parameters: resonance (Q 0.5-50), a bipolar envelope amount, and the
        envelope's decay (5 ms-10 s, restarted by each note).
      - Four real per-track parameters: k 28-31, CC 74 (cutoff) and CC 71
        (reso), NRPN 1:20/1:21. They work as LFO destinations too.
      - Cost: at most ~5,100 instructions per block with all six tracks
        filtered and modulated (their §9), about +6 % engine time.
      - Builds with the Syntakt engines: `-t
        syntakt-sd-cp-toy-bits-swarm,track-filter` (checked 2026-10-22).
      - Steps: rebuild `data-st` with both tweaks, then the step 16
        pipeline (boot, coverage with filter scenarios, capture, check,
        translate, pgo).
      - Plugin: a FILTER section per track (cutoff, reso, env, decay; a
        filter-response display like the LFO's), engine params at the new
        record offsets, CC 74/71, and LFO destinations if the tweak adds
        them to the LFO's list.

   h. **Filter types, after the Digitone II's** (Jack, 2026-10-23).
      - The track filter today is Modded-Cycles' SVF in the firmware (LP or
        HP).
      - The DN2's filters run on its SHARC DSP, which can't run in real
        time yet (the paused DN2 project), so they can't be moved over as
        code.
      - Route: a `--hook` where the mixer takes the finished track buffers
        (`0x80001858 + 128 t`, at the mixer's entry `0x400567ba`).
      - The plugin then runs its own per-track filters in place: types as
        the DN2's, from its firmware notes and code (`~/Documents/AI/
        digitone2-plugin`). Faithful re-creations, not bit-exact.
      - The DN2's types (its NOTES, sound record `0xb4 + 2r`): 0
        Multimode, 1 Lowpass 4, 2 Equalizer, 3 Comb-, 4 Legacy (LP/HP),
        5 Comb+.
   j. **Legato and portamento, as on the Syntakt** (Jack, 2026-10-23).
      Not in the Model:Cycles firmware: plugin-side, a held note's new note
      re-pitches the sounding voice (no retrigger), gliding its Pitch / Fine
      Tune words over a Glide time.
   k. CyclesFX: a smaller window (it shares the instrument's 960x780).
   i. **CHOMPI's tape record function** (Jack, 2026-10-23,
      https://github.com/CHOMPI-Club/CHOMPI). After everything else is
      finished and debugged: study how it works and how it could fit the
      plugin.

Future ideas (Jack, 2026-10-22): make it its own instrument rather than an
Elektron copy, for future builds; not scheduled yet.
- **Vector morphing**: take one sound and morph it into another, like a
  vector synth (e.g. an XY pad between 2-4 stored sounds, interpolating
  their parameters, or crossfading voices).
- **New ways to tweak the FM**, outside Elektron's macro knobs.
- **Polyphony** in the single-track "Cycles Voice" plugin (several engine
  voices or instances), if it is too heavy for the six-track one.

1. Sweep every CC over 0-127 (and note velocity 0, LFO destination) with
   `ccmap.py` so the clamps in `engine_inputs.py` are complete; read the
   pattern's per-track trig flags for all six tracks.
2. ColdFire AOT for the render's code, validated with replay.py and
   driven.py; target well under 0.667 ms per block.
3. Find the per-track split inside `FUN_4005979e` for the voice/FX plugins.
