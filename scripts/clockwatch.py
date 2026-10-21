#!/usr/bin/env python3
"""Which render inputs does MIDI clock change?

Phase 1 records every address the render (FUN_40058c5e) reads. Then MIDI
clock (0xF8, 24 per quarter note) is sent at one tempo and another, paced in
real time, and every write made outside the render to an address the render
reads is logged with its writer PC and values, per tempo.

    cd re/digiemu && uv run --no-sync python ../../scripts/clockwatch.py
"""
import collections
import os
import sys
import time

os.environ['DIGIKIT_PIT3_PROBE'] = '1'
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import emuhost  # noqa: E402

from unicorn import UC_HOOK_BLOCK, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE  # noqa: E402
from unicorn.m68k_const import UC_M68K_REG_PC, UC_M68K_REG_A7  # noqa: E402


def main():
    st = {'ret': None, 'reads': set(), 'learn': True, 'log': None}

    def block(uc, address, size, _):
        if st['ret'] is None:
            if address == emuhost.RENDER_ISR:
                sp = uc.reg_read(UC_M68K_REG_A7)
                st['ret'] = int.from_bytes(uc.mem_read(sp + 4, 4), 'big')
        elif address == st['ret']:
            st['ret'] = None

    def read(uc, access, address, size, value, _):
        if st['learn'] and st['ret'] is not None and address < 0x8c000000:
            for a in range(address & ~3, address + size, 4):
                st['reads'].add(a)

    def write(uc, access, address, size, value, _):
        if st['log'] is None or st['ret'] is not None:
            return
        if (address & ~3) in st['reads'] and not 0x404a0000 <= address < 0x404e0000:   # task stacks
            st['log'].append((uc.reg_read(UC_M68K_REG_PC), address, size,
                              value & ((1 << 8 * size) - 1)))

    def on_machine(m):
        m.uc.hook_add(UC_HOOK_BLOCK, block)
        m.uc.hook_add(UC_HOOK_MEM_READ, read)
        m.uc.hook_add(UC_HOOK_MEM_WRITE, write)

    emu = emuhost.start(on_machine)
    time.sleep(2.0)                       # learn the render's reads
    st['learn'] = False
    print('render reads %d words' % len(st['reads']))

    results = {}
    for bpm in (120, 60, 150):
        st['log'] = []
        period = 60.0 / bpm / 24
        t_end = time.monotonic() + 4.0
        nxt = time.monotonic()
        while time.monotonic() < t_end:
            emu.midi_in.put(b'\xf8')
            nxt += period
            time.sleep(max(0, nxt - time.monotonic()))
        log, st['log'] = st['log'], None
        by = collections.defaultdict(list)
        for pc, addr, size, v in log:
            by[(addr, size)].append((pc, v))
        results[bpm] = by
        print('== %d BPM clock: %d writes to render inputs, %d addresses' % (bpm, len(log), len(by)))
    emuhost.stop(emu)
    keys = set()
    for by in results.values():
        keys |= set(by)
    print('address/size: last value per tempo (writers)')
    for k in sorted(keys):
        cells = []
        for bpm in (120, 60, 150):
            ws = results[bpm].get(k)
            cells.append('%5d BPM %8s' % (bpm, '%x' % ws[-1][1] if ws else '-'))
        pcs = sorted({'%08x' % pc for by in results.values() for pc, _ in by.get(k, [])})
        print('  %08x/%d  %s   by %s' % (k[0], k[1], '  '.join(cells), ' '.join(pcs[:4])))


if __name__ == '__main__':
    main()
