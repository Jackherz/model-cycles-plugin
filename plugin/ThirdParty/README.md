# Third-party audio effects

The alternative delay and reverb engines (NOTES.md step 37). Each folder holds
the effect's source as published, with its licence and `SOURCE.txt` (the
repository and commit it came from). All are MIT-licensed.

| Folder | Effect | Origin | Licence |
|---|---|---|---|
| `cloudseed` | CloudSeed reverb | Schwung port (charlesvestal/schwung-cloudseed) of Ghost Note Audio's CloudSeedCore | MIT |
| `psxverb` | PlayStation 1 SPU reverb | charlesvestal/schwung-psxverb | MIT |
| `space-delay` | Ping-pong tape echo | charlesvestal/schwung-space-delay | MIT |
| `munchi-delay` | CHOMPI TEMPO's dual delay and reverb | charlesvestal/schwung-munchi-delay, a port of CHOMPI-Club/CHOMPI's TEMPO firmware (MIT, `LICENSE-CHOMPI`). It includes DaisySP (`daisysp/LICENSE`, MIT) and Mutable Instruments' reverb by Emilie Gillet (MIT, in `tempo/reverb.h` and `tempo/fx_engine.h`). | MIT |

`wrap/` is CyclesEmu's own:
- One wrapper per effect. Each includes the effect's source, renames its
  single exported entry point, and drives it through its own API at 48 kHz.
- Changes to vendored code, each marked `CyclesEmu` where it is made:
  - `space-delay/spacecho.c`: its `SAMPLE_RATE` define is guarded with
    `#ifndef`, so the wrapper can set 48 kHz.
  - `space-delay/spacecho.c`, `psxverb/psxverb.c`, `cloudseed/cloudseed.c`:
    a copy of each v2 block (`cyem_process_float`) on float samples, without
    the 16-bit step in and out. Its truncation ground decaying tails into
    fizz (NOTES.md step 39).
  - `psxverb/psxverb.c`: the 2x resampler. Its FIR taps were not the
    SPU's: every side tap was negative, for a DC gain of -0.58. They are now
    psx-spx's table. The interpolator also left every other output sample
    at 0, which mirrored the reverb about 24 kHz as loud as itself. Writes
    to the 16-bit work RAM round instead of truncating.
  - `munchi-delay/tempo/reverb.h`: the Clouds/Rings reverb keeps its delay
    memory in float, not 16-bit (`FORMAT_32_BIT`). The tail had grain that
    grew as it faded.
