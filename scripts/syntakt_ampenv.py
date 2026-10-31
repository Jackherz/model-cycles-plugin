#!/usr/bin/env python3
"""The Syntakt's amp envelope for CyclesEmu (NOTES.md step 44): its own DSP
function (OS 1.42 section 7, 0x40001476, the one Synthack-OS documents),
translated ahead of time like the Cycles render.

The DSP calls it once per voice per 32-frame block (0x4000168e):

    ampenv(buf, state, par, trig, release, voice, kill)

buf: the voice's 32 samples (Q31, shaped in place); state: 12 bytes (phase,
hold counter, level); par: the track's amp words from +106 (ATK +2, HOLD +4,
DEC +6, SUS +8, REL +10, MODE +22, AENR +24; 8.8 words, values in the high
byte); trig, release, kill: that voice's bits of the block's masks.

    cd re/digiemu && uv run --no-sync python ../../scripts/syntakt_ampenv.py SYNTAKT_SYX OUT_C STATE_DIR [--check]

writes OUT_C (the function, st_ampenv_run) and STATE_DIR/syntakt-dsp.bin (the
section, whose tables the function reads); with --check, compiles the
translation and compares it with the emulated function block by block on
scenarios the coverage did not see. Needs m68k-linux-gnu-objdump on PATH
(stengine's EMAC decoder; Homebrew's m68k-elf-objdump under that name).
"""
import ctypes as C
import json
import os
import random
import struct
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 're', 'modded-cycles', 'tools', 'emu'))
sys.path.insert(0, os.path.join(ROOT, 're', 'modded-cycles', 'tools'))
sys.path.insert(0, os.path.join(ROOT, 'aot'))

import emac  # noqa: E402
import stengine  # noqa: E402
import syntakt  # noqa: E402
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_BLOCK  # noqa: E402
from unicorn import m68k_const as mk  # noqa: E402

BASE = 0x40000400
AMPENV = 0x40001476
# our scratch layout, the same in the emulator and in the plugin's context
RET, BUF, STATE, PAR, STACK = 0x50000000, 0x50001000, 0x50002000, 0x50003000, 0x5000F000


class Reference:
    """The function in Unicorn, EMAC by stengine's corrected decoder."""

    def __init__(self, img):
        uc = self.uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        uc.ctl_set_cpu_model(mk.UC_CPU_M68K_ANY)
        uc.mem_map(0x40000000, 0x00080000)
        uc.mem_write(BASE, img)
        uc.mem_map(0x50000000, 0x00010000)
        uc.mem_write(RET, b'\x4e\x71\x4e\x71')
        self.emac = emac.EMAC(uc)
        self.emac.install(stengine._emac_instrs(img))
        self.emac.macsr = 0xa0
        self.blocks = set()

    def trace(self):
        self.uc.hook_add(UC_HOOK_BLOCK, lambda uc, a, s, u: self.blocks.add(a))

    def run(self, args):
        uc = self.uc
        sp = STACK - 4 * (len(args) + 1)
        uc.mem_write(sp, struct.pack('>%dI' % (len(args) + 1), RET, *args))
        uc.reg_write(mk.UC_M68K_REG_SR, 0x2700)
        uc.reg_write(mk.UC_M68K_REG_A7, sp)
        uc.emu_start(AMPENV, RET, count=200000)

    def write(self, a, b):
        self.uc.mem_write(a, bytes(b))

    def read(self, a, n):
        return bytes(self.uc.mem_read(a, n))


def par_block(rng):
    v = lambda: rng.randint(0, 127)
    hold = rng.choice([0, 1, 8, 30, 64, 100, 126, 127, v()])
    dec = rng.choice([0, 1, 10, 40, 90, 126, 127, v()])
    words = [0] * 16
    words[1], words[2], words[3] = rng.choice([0, 1, 5, 20, 60, 127, v()]) << 8, hold << 8, dec << 8
    words[4], words[5] = rng.choice([0, 30, 100, 127, v()]) << 8, rng.choice([0, 5, 32, 90, 127, v()]) << 8
    words[11], words[12] = rng.randint(0, 1) << 8, rng.randint(0, 1) << 8      # MODE (+22), AENR (+24)
    return struct.pack('>16H', *words)


def scenarios(seeds, blocks):
    """Per scenario: its parameter block and, per block, (input, trig, release, kill)."""
    for seed in seeds:
        rng = random.Random(seed)
        par = par_block(rng)
        events = []
        for b in range(blocks):
            trig = 1 if b == 1 or (rng.random() < 0.01) else 0
            rel = 1 if rng.random() < 0.02 else 0
            kill = 1 if rng.random() < 0.003 else 0
            if rng.random() < 0.5:
                x = [0x40000000] * 32
            else:
                x = [rng.randint(-0x7fffffff, 0x7fffffff) for _ in range(32)]
            events.append((struct.pack('>32i', *x), trig, rel, kill))
        yield seed, par, events


def play(target, par, events, voice=0):
    target.write(STATE, bytes(12))
    target.write(PAR, par)
    out = []
    for x, trig, rel, kill in events:
        target.write(BUF, x)
        target.run([BUF, STATE, PAR, trig, rel, voice, kill])
        out.append(target.read(BUF, 128) + target.read(STATE, 12))
    return out


class Translated:
    """The translation, compiled with the runtime into a library."""

    def __init__(self, c_path, img):
        aot = os.path.join(ROOT, 'aot')
        self.tmp = tempfile.mkdtemp()
        lib = os.path.join(self.tmp, 'libampenv.dylib')
        subprocess.run(['clang', '-O2', '-fPIC', '-shared', '-Wno-parentheses-equality',
                        '-I', os.path.join(aot, 'build-sttw'), '-o', lib,
                        os.path.join(aot, 'runtime.c'), c_path], check=True)
        from check_replay import Cpu
        self.Cpu = Cpu
        L = self.L = C.CDLL(lib)
        L.mc_ctx_new.restype = C.c_void_p
        L.st_ampenv_run.argtypes = [C.c_void_p, C.POINTER(Cpu), C.c_uint32, C.c_uint32]
        L.mc_mem_write.argtypes = [C.c_void_p, C.c_uint32, C.c_char_p, C.c_uint32]
        L.mc_mem_read.argtypes = [C.c_void_p, C.c_uint32, C.c_void_p, C.c_uint32]
        self.x = L.mc_ctx_new()
        self.write(BASE, img)
        self.write(RET, b'\x4e\x71\x4e\x71')

    def write(self, a, b):
        b = bytes(b)
        self.L.mc_mem_write(self.x, a, b, len(b))

    def read(self, a, n):
        buf = C.create_string_buffer(n)
        self.L.mc_mem_read(self.x, a, buf, n)
        return buf.raw

    def run(self, args):
        sp = STACK - 4 * (len(args) + 1)
        self.write(sp, struct.pack('>%dI' % (len(args) + 1), RET, *args))
        cpu = self.Cpu()
        cpu.a[7] = sp
        cpu.sr = 0x2700
        cpu.macsr = 0xa0
        cpu.mac_mask = 0xffffffff
        st = self.L.st_ampenv_run(self.x, C.byref(cpu), AMPENV, RET)
        assert st == 0, 'status %d' % st


def main():
    syx, out_c, state_dir = sys.argv[1:4]
    check = '--check' in sys.argv
    img = syntakt.dsp_image(syx)
    os.makedirs(state_dir, exist_ok=True)
    with open(os.path.join(state_dir, 'syntakt-dsp.bin'), 'wb') as f:
        f.write(img)
    # coverage: the function under every phase, setting and event we can think of
    ref = Reference(img)
    ref.trace()
    for seed, par, events in scenarios(range(60), 400):
        play(ref, par, events, voice=seed % 8)
    import translate as T
    table = json.load(open(os.path.join(ROOT, 'aot', 'opcode_table.json')))
    tr = T.Translator(img, table, [])
    tr.discover(sorted(ref.blocks | {AMPENV}))
    with open(out_c, 'w') as f:
        missing = tr.emit_c(f, name='st_ampenv_run', what="the Syntakt's amp envelope (DSP 0x40001476)")
    print('%d instructions, %d labels, %d coverage roots -> %s' % (len(tr.insns), len(tr.labels), len(ref.blocks), out_c))
    if missing or tr.unsupported:
        raise SystemExit('!! missing %s, unsupported %s' % (sorted(missing)[:5], tr.unsupported))
    if not check:
        return
    aot = Translated(out_c, img)
    total = bad = 0
    for seed, par, events in scenarios(range(1000, 1040), 600):
        a, b = play(Reference(img), par, events, voice=seed % 8), play(aot, par, events, voice=seed % 8)
        diff = sum(x != y for x, y in zip(a, b))
        total += len(a)
        bad += diff
        if diff:
            print('scenario %d: %d of %d blocks differ (first %d)' % (seed, diff, len(a), next(i for i, (x, y) in enumerate(zip(a, b)) if x != y)))
    print('%d/%d blocks bit-exact (the 32 samples and the 12-byte state)' % (total - bad, total))
    if bad:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
