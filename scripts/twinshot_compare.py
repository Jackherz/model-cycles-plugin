#!/usr/bin/env python3
"""SP TWINSHOT in CyclesEmu against the Syntakt's own engine (NOTES.md step 38).

The reference is the Syntakt OS's real code run in Unicorn (Modded-Cycles'
tools/emu/stengine.py: its voice loop, machine type 44). Ours is the AOT
engine (aot/build-sttw) through its C API. Both get the same samples: the
48 kHz files of the Twinshot folder, loaded unchanged as 16-bit (so the data
is identical), at the same table slots, with the same settings. Our output
is the track buffer after the Cycles' Vol+Dist, so the shapes are compared
(normalised correlation, best lag) along with the level ratio.

    re/digiemu/.venv/bin/python scripts/twinshot_compare.py FOLDER [STATE]
    (run from re/digiemu: uv run --no-sync python ../../scripts/twinshot_compare.py ...)
"""
import ctypes
import pathlib
import struct
import sys
import wave

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "re/modded-cycles/tools/emu"))
sys.path.insert(0, str(ROOT / "re/modded-cycles/tools"))
import stengine  # noqa: E402
import syntakt  # noqa: E402

TABLE = 0x4404f754          # the Syntakt's sample table ({length, pointer} x 64)
OUR_TABLE = 0x43030754      # the same, through the port's BSS window
DATA = 0x60000000
BLOCKS = 300


def natural(name):
    import re
    return [int(t) if t.isdigit() else t.lower() for t in re.split(r"(\d+)", name)]


def bank(folder):
    """[(name, rate, int16 samples)] in the plugin's order (natural name order, 64 at most)."""
    files = sorted([f for f in pathlib.Path(folder).iterdir()
                    if f.suffix.lower() in (".wav",) and not f.name.startswith(".")], key=lambda f: natural(f.name))
    out = []
    for f in files[:64]:
        with wave.open(str(f)) as w:
            if w.getsampwidth() != 2:
                out.append((f.stem, 0, None))
                continue
            x = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, w.getnchannels()).mean(axis=1)
            out.append((f.stem, w.getframerate(), np.round(x).astype(np.int16)))
    return out


def table_bytes(samples, base):
    tab, blob, at = bytearray(64 * 8), bytearray(), base
    for i, (_, _, x) in enumerate(samples):
        d = (x if x is not None else np.zeros(1, np.int16)).astype(">i2").tobytes()
        struct.pack_into(">II", tab, 8 * i, len(d) // 2, at + len(blob))
        blob += d + bytes((-len(d)) % 16)
    return bytes(tab), bytes(blob)


def reference(samples, s1, s2, mix, blocks=BLOCKS):
    img = syntakt.dsp_image(str(ROOT / "firmware/Syntakt_OS1.42.syx"))
    e = stengine.Engine(img)
    e.uc.mem_map(DATA, 0x02000000)
    tab, blob = table_bytes(samples, DATA)
    e.uc.mem_write(DATA, blob)
    e.uc.mem_write(TABLE, tab)
    e.solo(0)
    e.machine(0, 44)
    e.note(0, 60)   # the trig's note, as Modded-Cycles' comparisons (compare_sdvintage.py): 60 << 16
    e.set(0, tune=64, p1=s1, p2=64, p3=mix, p4=64, punch=s2, gate=0, decay=127, over=0)
    x = e.render(blocks, trig_at=(1,), track=0).astype(np.float64)
    assert not e.unmapped, e.unmapped[:3]
    return x


def ours(samples, s1, s2, mix, state, blocks=BLOCKS):
    lib = ctypes.CDLL(str(ROOT / "aot/build-sttw/libmcengine.dylib"))
    lib.mc_engine_new.restype = ctypes.c_void_p
    lib.mc_engine_new.argtypes = [ctypes.c_char_p, ctypes.POINTER(ctypes.c_int)]
    for fn, args in (("mc_engine_param", [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]),
                     ("mc_engine_twin_param", [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int]),
                     ("mc_engine_note", [ctypes.c_void_p, ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                         ctypes.c_int]),
                     ("mc_engine_write", [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_char_p, ctypes.c_uint32]),
                     ("mc_engine_read", [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_char_p, ctypes.c_uint32]),
                     ("mc_engine_render", [ctypes.c_void_p, ctypes.POINTER(ctypes.c_float),
                                           ctypes.POINTER(ctypes.c_float)]),
                     ("mc_engine_level", [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]),
                     ("mc_engine_snap_param", [ctypes.c_void_p, ctypes.c_int, ctypes.c_int])):
        getattr(lib, fn).argtypes = args
    err = ctypes.c_int()
    g = lib.mc_engine_new(str(state).encode(), ctypes.byref(err))
    assert g, f"engine state: error {err.value}"
    tab, blob = table_bytes(samples, DATA)
    lib.mc_engine_write(g, DATA, blob, len(blob))
    lib.mc_engine_write(g, OUR_TABLE, tab, len(tab))
    for t in range(1, 6):
        lib.mc_engine_level(g, t, 0)
    lib.mc_engine_param(g, 0, 0x26, 11)                       # SP TWINSHOT
    for off, v in ((0x28, 64), (0x2a, s1), (0x2c, 64), (0x2e, mix), (0x30, 64), (0x34, 0), (0x38, 127),
                   (0x3a, 64), (0x3c, 0), (0x3e, 0)):
        lib.mc_engine_param(g, 0, off, v)
    lib.mc_engine_twin_param(g, 0, 0, s2)                     # Sample 2
    lib.mc_engine_twin_param(g, 0, 1, 0)                      # Overdrive
    for off in (0x26, 0x28, 0x2a, 0x2c, 0x2e, 0x30, 0x34, 0x38, 0x3a, 0x3c, 0x3e):
        lib.mc_engine_snap_param(g, 0, off)                   # settled now, as the plugin's after its first second
    L, R = (ctypes.c_float * 32)(), (ctypes.c_float * 32)()
    buf = ctypes.create_string_buffer(128)
    out = []
    for b in range(blocks + 40):
        if b == 40:
            lib.mc_engine_note(g, 0, 60, 127, 1, 0x0681)
        lib.mc_engine_render(g, L, R)
        if b >= 39:
            lib.mc_engine_read(g, 0x80001858, buf, 128)
            out.append(np.frombuffer(buf.raw, dtype=">i4").astype(np.float64))
    return np.concatenate(out)


def compare(a, b):
    """Best normalised correlation over lags of +-2 blocks, and the level ratio there."""
    n = min(len(a), len(b)) - 128
    best = (-2.0, 0, 0.0)
    for lag in range(-64, 65):
        x, y = a[64:64 + n - 64], b[64 + lag:64 + lag + n - 64]
        if not (np.any(x) and np.any(y)):
            continue
        c = float(np.dot(x, y) / np.sqrt(np.dot(x, x) * np.dot(y, y)))
        if c > best[0]:
            best = (c, lag, float(np.sqrt(np.dot(y, y) / np.dot(x, x))))
    return best


def main():
    folder = sys.argv[1]
    state = sys.argv[2] if len(sys.argv) > 2 else str(pathlib.Path.home() / "Library/Application Support/CyclesEmu/state")
    samples = bank(folder)
    native = [i for i, (_, rate, x) in enumerate(samples) if rate == 48000 and x is not None]
    # only 48 kHz files at the slots compared: keep the others (unchanged slots) as they are
    print(f"{len(samples)} samples, 48 kHz at slots {native}")
    ok = True
    for s1i, s2i, mix in ((native[0], native[1], 0), (native[0], native[1], 127), (native[0], native[1], 64),
                          (native[2], native[3], 64)):
        for rev in (False, True):
            s1 = (63 - s1i) if rev else 64 + s1i
            s2 = (63 - s2i) if rev else 64 + s2i
            ref, our = reference(samples, s1, s2, mix), ours(samples, s1, s2, mix, state)
            c, lag, ratio = compare(ref, our)
            ok &= c > 0.99
            print(f"  samples {samples[s1i][0]!r} + {samples[s2i][0]!r}{' reversed' if rev else ''}, mix {mix}: "
                  f"correlation {c:.5f} at lag {lag}, level x{ratio:.3f}  (ref rms {np.sqrt(np.mean(ref**2)):.0f})")
    print("twinshot against the Syntakt's engine ->", "PASS" if ok else "FAIL")


if __name__ == "__main__":
    main()
