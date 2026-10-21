#!/usr/bin/env python3
"""Re-run captured Model:Cycles render calls standalone and compare them
bit for bit with digiemu (scripts/capture.py).

A bare Unicorn (ColdFire V4e, the patched build digiemu uses) gets the base
memory dump. Each call then: its entry pages are written, its CPU context
restored, and FUN_40058c5e runs from the top until the PC the interrupt
frame returns to. Peripheral reads get the values digiemu's models gave, in
order. Afterwards every page the call touched must equal digiemu's, and the
peripheral writes must match in order.

    cd re/digiemu && uv run --no-sync python ../../scripts/replay.py ../../data/cap-test
"""
import argparse
import gzip
import os
import pickle
import sys
import time

from unicorn import (Uc, UcError, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE,
                     UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE, UC_HOOK_INTR)
from unicorn.m68k_const import UC_CPU_M68K_CFV4E, UC_M68K_REG_D0, UC_M68K_REG_A0

REGS = [UC_M68K_REG_D0 + i for i in range(8)] + [UC_M68K_REG_A0 + i for i in range(8)]

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                                're', 'digiemu'))
from emu import native, edma_sw  # noqa: E402

PAGE = 0x1000
MMIO = ((0x8c000000, 0x8c0fffff), (0xec000000, 0xec0fffff), (0xfc000000, 0xfc0fffff))
RING = 0x4a3ed000
DMA_REGIONS = ((0x4a300000, 0x4a7fffff), (0x80000000, 0x800fffff))
REGION_PAGES = [p for lo, hi in DMA_REGIONS for p in range(lo, hi + 1, PAGE)]           # page holding the SSI output ring (0x4a3ed080, 512 B)


class Mismatch(Exception):
    pass


class Replay:
    def __init__(self, base):
        uc = self.uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        uc.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
        # RTE executed in the core, as digiemu runs it (stock Unicorn stops
        # with EXCP_RTE, 256, and leaves the return to the host).
        if not native.enable_options(uc, native.NATIVE_RTE) & native.NATIVE_RTE:
            sys.exit('this Unicorn has no NATIVE_RTE (install the patched build)')
        for b, e, data in base:
            uc.mem_map(b, e - b + 1)
            uc.mem_write(b, data)
        # The render starts memory-to-memory eDMA transfers (channel 30's TCD
        # at 0xFC0453C0) and polls for DONE: digiemu's software eDMA bank runs
        # them, natively when the patched Unicorn has it. Installed before the
        # replay's own peripheral hooks, as in digiemu.
        self.machine = type('M', (), {})()
        self.machine.uc = uc
        self.edma = edma_sw.SoftwareBank(self.machine)
        self.reads = self.writes = None
        self.compare_s = 0.0
        for lo, hi in MMIO:
            uc.hook_add(UC_HOOK_MEM_READ, self._mmio_read, begin=lo, end=hi)
            uc.hook_add(UC_HOOK_MEM_WRITE, self._mmio_write, begin=lo, end=hi)
        uc.hook_add(UC_HOOK_INTR, self._intr)
        self.trace = None
        self._code_hook = None

    def _code(self, uc, address, size, _):
        if self.trace is not None:
            self.trace.append((address,) + tuple(uc.reg_read(r) for r in REGS))

    def _mmio_read(self, uc, access, address, size, value, _):
        if not self.reads:
            raise Mismatch('extra peripheral read %08x/%d' % (address, size))
        a, s, v = self.reads.pop(0)
        if (a, s) != (address, size):
            raise Mismatch('peripheral read %08x/%d, digiemu read %08x/%d'
                           % (address, size, a, s))
        uc.mem_write(address, v.to_bytes(size, 'big'))

    def _mmio_write(self, uc, access, address, size, value, _):
        self.writes.append((address, size, value & ((1 << 8 * size) - 1)))

    def _intr(self, uc, intno, _):
        raise Mismatch('exception %d inside the render' % intno)

    def call(self, c, isr):
        uc = self.uc
        for p, data in c['entry'].items():
            uc.mem_write(p, data)
        uc.context_restore(pickle.loads(c['ctx']))
        self.reads, self.writes = list(c['mmio_r']), []
        self.trace = [] if 'trace' in c else None
        if self.trace is not None and self._code_hook is None:
            self._code_hook = uc.hook_add(UC_HOOK_CODE, self._code)
        try:
            uc.emu_start(isr, c['ret'], count=5_000_000)
        except UcError as exc:
            raise Mismatch('unicorn: %s' % exc)
        finally:
            if self.trace is not None:
                compare_traces(c['trace'], self.trace)
                self.trace = None
        self.edma.service(0)
        if self.reads:
            raise Mismatch('%d peripheral reads not made' % len(self.reads))
        if self.writes != c['mmio_w']:
            for i, (got, want) in enumerate(zip(self.writes, c['mmio_w'])):
                if got != want:
                    raise Mismatch('peripheral write %d: %s, digiemu %s' % (i, got, want))
            raise Mismatch('%d peripheral writes, digiemu %d'
                           % (len(self.writes), len(c['mmio_w'])))


NAMES = ['D%d' % i for i in range(8)] + ['A%d' % i for i in range(8)]


def compare_traces(want, got):
    for i, (w, g) in enumerate(zip(want, got)):
        if w != g:
            print('  trace diverges at step %d of %d' % (i, len(want)))
            for j in range(max(0, i - 3), i + 2):
                if j < len(want):
                    print('   digiemu %6d pc %08x %s' % (j, want[j][0], ' '.join(
                        '%s=%08x' % (n, v) for n, v in zip(NAMES, want[j][1:]))))
                if j < len(got):
                    print('   replay  %6d pc %08x %s' % (j, got[j][0], ' '.join(
                        '%s=%08x' % (n, v) for n, v in zip(NAMES, got[j][1:]))))
            return
    print('  traces agree over %d steps (digiemu %d, replay %d)'
          % (min(len(want), len(got)), len(want), len(got)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('capture')
    ap.add_argument('--stop', action='store_true', help='stop at the first mismatch')
    args = ap.parse_args()
    with gzip.open(os.path.join(args.capture, 'base.pkl.gz')) as f:
        base = pickle.load(f)
    with gzip.open(os.path.join(args.capture, 'calls.pkl.gz')) as f:
        calls = pickle.load(f)
    isr = 0x40058c5e

    r = Replay(base)

    def base_page(p):
        for b, e, data in base:
            if b <= p <= e:
                return data[p - b:p - b + PAGE]
        raise KeyError('%08x not mapped' % p)

    expected = {}               # page -> digiemu's contents after the last call
    ok = bad = 0
    t0 = time.perf_counter()
    for k, c in enumerate(calls):
        expected.update(c['entry'])
        expected.update(c['exit'])
        try:
            r.call(c, isr)
            tc = time.perf_counter()
            diffs = []
            for p in list(c['touched']) + REGION_PAGES:
                want = expected.get(p)
                if want is None:
                    want = expected[p] = base_page(p)
                got = bytes(r.uc.mem_read(p, PAGE))
                if got != want:
                    first = next(i for i in range(PAGE) if got[i] != want[i])
                    diffs.append('%08x' % (p + first))
                    r.uc.mem_write(p, want)     # resync, so later calls test alone
            r.compare_s += time.perf_counter() - tc
            if diffs:
                raise Mismatch('pages differ, first bytes at ' + ' '.join(diffs[:8]))
            ok += 1
        except Mismatch as exc:
            bad += 1
            print('call %d: MISMATCH %s' % (k, exc))
            if args.stop:
                break
    dt = time.perf_counter() - t0 - r.compare_s
    print('%d/%d calls bit-exact (%d mismatched), %.2f ms per call in Unicorn (excluding the comparison)'
          % (ok, ok + bad, bad, 1e3 * dt / max(1, ok + bad)))
    ring_calls = sum(1 for c in calls if RING in c['exit'])
    print('%d calls changed the output ring page' % ring_calls)


if __name__ == '__main__':
    main()
