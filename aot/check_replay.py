#!/usr/bin/env python3
"""Run captured render calls through the AOT-compiled render and compare
them bit for bit with digiemu (the same check as scripts/replay.py, with
aot/build/libmcrender.dylib in place of Unicorn).

    cd re/digiemu && uv run --no-sync python ../../aot/check_replay.py ../../data/cap-chord
"""
import argparse
import ctypes as C
import gzip
import os
import pickle
import struct
import sys
import time

from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN
from unicorn.m68k_const import (UC_CPU_M68K_CFV4E, UC_M68K_REG_D0, UC_M68K_REG_A0,
                                UC_M68K_REG_SR)

P = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PAGE = 0x1000
RENDER_ISR = 0x40058c5e
DMA_REGIONS = ((0x4a300000, 0x4a7fffff), (0x80000000, 0x800fffff))
REGION_PAGES = [p for lo, hi in DMA_REGIONS for p in range(lo, hi + 1, PAGE)]
# Offsets of the EMAC state in a Unicorn m68k context (found by loading known
# values with to_mac/to_macsr/to_mask and searching the saved context).
CTX_MACC, CTX_MACSR, CTX_MASK = 320, 352, 356
STATUS = {0: 'done', 1: 'unknown target', 2: 'bad SR', 3: 'fault'}


class Cpu(C.Structure):
    _fields_ = [('d', C.c_uint32 * 8), ('a', C.c_uint32 * 8), ('pc', C.c_uint32),
                ('sr', C.c_uint32), ('cc_x', C.c_uint32), ('cc_n', C.c_uint32),
                ('cc_z', C.c_uint32), ('cc_v', C.c_uint32), ('cc_c', C.c_uint32),
                ('macsr', C.c_uint32), ('mac_mask', C.c_uint32), ('macc', C.c_uint64 * 4)]


class MmioRec(C.Structure):
    _fields_ = [('addr', C.c_uint32), ('size', C.c_uint32), ('value', C.c_uint32)]


class Aot:
    def __init__(self, lib=os.path.join(P, 'aot', 'build', 'libmcrender.dylib')):
        self.lib = L = C.CDLL(lib)
        L.mc_ctx_new.restype = C.c_void_p
        L.mc_run.argtypes = [C.c_void_p, C.POINTER(Cpu), C.c_uint32, C.c_uint32]
        L.mc_mem_write.argtypes = [C.c_void_p, C.c_uint32, C.c_char_p, C.c_uint32]
        L.mc_mem_read.argtypes = [C.c_void_p, C.c_uint32, C.c_void_p, C.c_uint32]
        L.mc_ctx_mmio_count.argtypes = [C.c_void_p]
        L.mc_ctx_mmio_count.restype = C.c_uint32
        L.mc_ctx_mmio_log.argtypes = [C.c_void_p]
        L.mc_ctx_mmio_log.restype = C.POINTER(MmioRec)
        L.mc_ctx_mmio_clear.argtypes = [C.c_void_p]
        self.x = L.mc_ctx_new()
        if not self.x:
            raise SystemExit('mc_ctx_new failed')
        self.uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        self.uc.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)

    def write(self, addr, data):
        self.lib.mc_mem_write(self.x, addr, bytes(data), len(data))

    def read(self, addr, n):
        buf = C.create_string_buffer(n)
        self.lib.mc_mem_read(self.x, addr, buf, n)
        return buf.raw

    def cpu_from_context(self, ctx_pickle):
        ctx = pickle.loads(ctx_pickle)
        self.uc.context_restore(ctx)
        raw = ctx.__getstate__()[0]
        cpu = Cpu()
        for i in range(8):
            cpu.d[i] = self.uc.reg_read(UC_M68K_REG_D0 + i)
            cpu.a[i] = self.uc.reg_read(UC_M68K_REG_A0 + i)
        sr = self.uc.reg_read(UC_M68K_REG_SR)
        cpu.sr = sr & 0xffe0
        cpu.cc_c = sr & 1
        cpu.cc_v = 0xffffffff if sr & 2 else 0
        cpu.cc_z = 0 if sr & 4 else 1
        cpu.cc_n = 0xffffffff if sr & 8 else 0
        cpu.cc_x = (sr >> 4) & 1
        cpu.macsr, cpu.mac_mask = struct.unpack_from('<II', raw, CTX_MACSR)
        for i in range(4):
            cpu.macc[i] = struct.unpack_from('<Q', raw, CTX_MACC + 8 * i)[0]
        return cpu

    def run(self, cpu, entry, stop):
        L = self.lib
        L.mc_ctx_mmio_clear(self.x)
        st = L.mc_run(self.x, C.byref(cpu), entry, stop)
        n = min(L.mc_ctx_mmio_count(self.x), 256)
        log = L.mc_ctx_mmio_log(self.x)
        return st, [(log[i].addr, log[i].size, log[i].value) for i in range(n)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('capture')
    ap.add_argument('--stop', action='store_true', help='stop at the first mismatch')
    args = ap.parse_args()
    with gzip.open(os.path.join(args.capture, 'base.pkl.gz')) as f:
        base = pickle.load(f)
    with gzip.open(os.path.join(args.capture, 'calls.pkl.gz')) as f:
        calls = pickle.load(f)

    aot = Aot()
    for b, e, data in base:
        aot.write(b, data)

    def base_page(p):
        for b, e, data in base:
            if b <= p <= e:
                return data[p - b:p - b + PAGE]
        raise KeyError('%08x' % p)

    expected = {}
    ok = bad = 0
    t_run = 0.0
    for k, c in enumerate(calls):
        for p, data in c['entry'].items():
            aot.write(p, data)
            expected[p] = data
        expected.update(c['exit'])
        seen = set()
        for a, sz, v in c['mmio_r']:        # what digiemu's peripherals answered
            if a not in seen and a != 0xfc0453de:
                aot.write(a, v.to_bytes(sz, 'big'))
                seen.add(a)
        cpu = aot.cpu_from_context(c['ctx'])
        t0 = time.perf_counter()
        st, writes = aot.run(cpu, RENDER_ISR, c['ret'])
        t_run += time.perf_counter() - t0
        problems = []
        if st != 0:
            problems.append('status %s at %08x' % (STATUS.get(st, st), cpu.pc))
        if writes != c['mmio_w']:
            for i, (g, w) in enumerate(zip(writes, c['mmio_w'])):
                if g != w:
                    problems.append('peripheral write %d: %08x/%d=%x, digiemu %08x/%d=%x'
                                    % ((i,) + g + w))
                    break
            else:
                problems.append('%d peripheral writes, digiemu %d' % (len(writes), len(c['mmio_w'])))
        diffs = []
        for p in list(c['touched']) + REGION_PAGES:
            want = expected.get(p)
            if want is None:
                want = expected[p] = base_page(p)
            got = aot.read(p, PAGE)
            if got != want:
                first = next(i for i in range(PAGE) if got[i] != want[i])
                diffs.append('%08x' % (p + first))
                aot.write(p, want)
        if diffs:
            problems.append('pages differ at ' + ' '.join(diffs[:8]))
        if problems:
            bad += 1
            print('call %d: %s' % (k, '; '.join(problems)))
            if args.stop:
                break
        else:
            ok += 1
    print('%d/%d calls bit-exact (%d mismatched), %.3f ms per call in the AOT render'
          % (ok, ok + bad, bad, 1e3 * t_run / max(1, ok + bad)))


if __name__ == '__main__':
    main()
