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
- The only change to vendored code is in `space-delay/spacecho.c`: its
  `SAMPLE_RATE` define is guarded with `#ifndef`, so the wrapper can set
  48 kHz. This is noted in its `SOURCE.txt`.
