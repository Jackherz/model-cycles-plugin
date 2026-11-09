# CyclesEmu

Unofficial plug-ins (AU, VST3, standalone; macOS) that run the Elektron Model:Cycles'
own sound engine, taken from firmware you supply and compiled ahead of time to native
code. A personal research project.

**No firmware is included, and nothing in this repository is derived from it.** The
plug-ins only work once you have built an engine from your own copy of the Model:Cycles
firmware. This project is not affiliated with or endorsed by Elektron.

## The plug-ins

| Plug-in | What it is |
|---|---|
| **CyclesEmu** | The instrument: eight tracks, each a Model:Cycles machine with its own parameters, LFOs, filter and mixer strip, plus the shared delay and reverb |
| **CyclesVoice** | One sound played polyphonically, on up to eight voices |
| **CyclesFX** | The Model:Cycles delay and reverb as an audio effect |

## How it works

The Model:Cycles' sound engine is ColdFire machine code. [`aot/translate.py`](aot/translate.py)
translates the parts of the firmware that render audio into C, ahead of time, and the
plug-ins call that render 1,500 times a second, as the hardware does. The translated
render is checked against the [digiemu](https://github.com/irpina/digiemu) emulator:
replaying captured blocks, it matches bit for bit. Eight tracks playing take about 11% of
one core on an M1 Pro (128-sample buffers); full emulation in digiemu runs at about 0.8x
real time on the same machine.

Two engines run side by side for eight tracks (the firmware has six), sharing one delay
and reverb. Everything around the engine is the plug-ins' own:

- Per-track pages for the machine, amp, filter (Model:Cycles' own or modelled Digitone II
  filter types) and two LFOs, with velocity and key modulation, legato and portamento.
- Optional Syntakt engines (SD Vintage, CP Vintage, SY Toy, SY Bits, SY Swarm and the
  SP Twinshot sample machine), added to the firmware with
  [Modded-Cycles](https://github.com/18nelli18/Modded-Cycles) from a Syntakt firmware you
  also supply.
- Alternative delay and reverb engines (see [Credits](#credits)).
- Kits and track presets, randomise and vary, undo, a mixer with mute and solo, colour
  schemes, and a help mode.
- MIDI: one channel per track, a drum map from C1, or the keys on one track. The CC map
  follows the hardware's.

The full development log, measurements and design decisions are in [NOTES.md](NOTES.md).

## Building

This is not a quick build. It needs:

- macOS on Apple Silicon, Xcode command line tools, CMake 3.22+, Ninja, Python 3 and
  [uv](https://docs.astral.sh/uv/)
- Model:Cycles firmware OS 1.13 (`.syx`), and Syntakt OS 1.42 for the Syntakt engines
- [digiemu](https://github.com/irpina/digiemu) at `f57c2f2`, cloned to `re/digiemu`,
  with its patched Unicorn
- for the Syntakt engines, [Modded-Cycles](https://github.com/18nelli18/Modded-Cycles)
  at `2594419`, cloned to `re/modded-cycles`, with
  [`patches/modded-cycles-cyclesemu.patch`](patches/modded-cycles-cyclesemu.patch) applied

The steps, each described in [NOTES.md](NOTES.md) (steps 3-6, 16 and 43):

1. *(Syntakt engines only)* Build the modified OS from both firmware files with
   Modded-Cycles' `tools/build.py`.
2. Run [`scripts/firmware_pipeline.sh`](scripts/firmware_pipeline.sh): it boots the
   firmware in digiemu, records which code the sound engine runs, captures an engine
   state and dumps it. Expect about an hour.
3. Copy the dumped state to `~/Library/Application Support/CyclesEmu/state`. The
   plug-ins load it at run time; without it they are silent.
4. Translate the render with `aot/translate.py` (the coverage and state from step 2),
   and optionally train a profile with `aot/pgo.sh` for a faster engine.
5. Build the plug-ins:

   ```bash
   cmake -S plugin -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ```

   They are installed to `~/Library/Audio/Plug-Ins/`. The render takes a few minutes to
   compile the first time.

Everything the pipeline produces from firmware (`firmware/`, `data*/`, the translated
`render.c`, captures and screenshots) is listed in [`.gitignore`](.gitignore) and must
stay out of the repository.

## Tests

`plugin/Test/` builds four console apps: `CyclesEmuHostTest` (hosts the built VST3 and
checks its output against the engine, sample for sample), `CyclesEmuLoadTest`,
`CyclesEmuResamplerTest` and `CyclesEmuEditorShot` (renders the editor and checks its
behaviour). `auval -v aumu Cyem Pers` validates the instrument.

## Credits

- [digiemu](https://github.com/irpina/digiemu), whose emulation is the reference every
  translated render is checked against.
- [Modded-Cycles](https://github.com/18nelli18/Modded-Cycles), for the Syntakt engines
  and the track filter.
- The alternative delay and reverb engines in [`plugin/ThirdParty/`](plugin/ThirdParty),
  all MIT-licensed and taken from charlesvestal's Schwung ports: CloudSeed (Ghost Note
  Audio's CloudSeedCore), the PlayStation SPU reverb, a ping-pong tape echo, and CHOMPI's
  TEMPO delay and reverb (with DaisySP and Mutable Instruments' reverb by Emilie Gillet).
  Each folder keeps its licence and where it came from.
- The CC map from [midi.guide](https://midi.guide/d/elektron/model-cycles/).
- [JUCE](https://juce.com) 8.0.4, under its AGPLv3 licence.

## Licence

No licence has been chosen for this project's own code yet, so it is all rights reserved
for now. Third-party code keeps its own licence, in its folder. Model:Cycles, Syntakt and
Digitone are trademarks of Elektron.
