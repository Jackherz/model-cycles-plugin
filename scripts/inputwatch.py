#!/usr/bin/env python3
"""Which firmware code writes which memory when one MIDI message arrives?

Runs digiemu, lets it settle, then records every guest write made OUTSIDE
the block render (FUN_40058c5e) for --span emulated instructions, twice: a
quiet window, then a window that starts with the MIDI bytes given. Writes
are grouped by (writer PC, 16-byte line); the report lists what the MIDI
window wrote that the quiet window did not, with the writers.

PC sync in memory hooks is on for this tool (DIGIKIT_PIT3_PROBE=1 stops
digiemu enabling NO_HOOK_PC_SYNC), so the writer PC is exact.

    cd re/digiemu && uv run --no-sync python ../../scripts/inputwatch.py --msg b0:50:0a
"""
import argparse
import collections
import json
import os
import sys
import threading
import time

os.environ['DIGIKIT_PIT3_PROBE'] = '1'
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import emuhost  # noqa: E402

from unicorn import UC_HOOK_BLOCK, UC_HOOK_MEM_WRITE  # noqa: E402
from unicorn.m68k_const import UC_M68K_REG_A7, UC_M68K_REG_PC  # noqa: E402

DDR_LO, DDR_HI = 0x40000000, 0x4fffffff
SRAM_LO, SRAM_HI = 0x80000000, 0x800fffff


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--msg', action='append', required=True,
                    help='MIDI bytes in hex, colon separated (b0:50:0a)')
    ap.add_argument('--span', type=emuhost.parse_n, default=emuhost.parse_n('16M'))
    ap.add_argument('--cc', action='append', default=[], help='setup CCs (CH:CC:VAL)')
    ap.add_argument('--out', default=os.path.join(emuhost.P, 'notes', 'inputwatch.json'))
    args = ap.parse_args()

    st = {'win': None, 'ret': None, 'w': {'quiet': collections.Counter(),
                                         'midi': collections.Counter()},
          'vals': {}}

    def block(uc, address, size, _):
        if st['ret'] is None:
            if address == emuhost.RENDER_ISR:
                sp = uc.reg_read(UC_M68K_REG_A7)
                st['ret'] = int.from_bytes(uc.mem_read(sp + 4, 4), 'big')
        elif address == st['ret']:
            st['ret'] = None

    def write(uc, access, address, size, value, _):
        win = st['win']
        if win is None or st['ret'] is not None:
            return
        if not (DDR_LO <= address <= DDR_HI or SRAM_LO <= address <= SRAM_HI):
            return
        pc = uc.reg_read(UC_M68K_REG_PC)
        key = (pc, address & ~0xF)
        st['w'][win][key] += 1
        if win == 'midi':
            st['vals'].setdefault(key, []).append((address, size, value))

    def on_machine(m):
        m.uc.hook_add(UC_HOOK_BLOCK, block)
        m.uc.hook_add(UC_HOOK_MEM_WRITE, write)

    emu = emuhost.start(on_machine)
    time.sleep(1.0)
    emuhost.send_ccs(emu, args.cc)
    emu.realtime = False

    def window(name, msgs=()):
        i0 = emu.stats['instrs']
        st['win'] = name
        for msg in msgs:
            emu.midi_in.put(bytes(int(b, 16) for b in msg.split(':')))
        while emu.stats['instrs'] - i0 < args.span:
            time.sleep(0.01)
        st['win'] = None

    window('quiet')
    window('midi', args.msg)
    emuhost.stop(emu)

    quiet_lines = {line for _, line in st['w']['quiet']}
    quiet_pcs = {pc for pc, _ in st['w']['quiet']}
    new = [(k, n) for k, n in st['w']['midi'].items()
           if k[1] not in quiet_lines or k[0] not in quiet_pcs]
    new.sort(key=lambda kn: (kn[0][1], kn[0][0]))
    by_pc = collections.Counter()
    for (pc, line), n in new:
        by_pc[pc] += n
    out = {'msg': args.msg, 'span': args.span,
           'writes': [{'pc': '0x%08x' % pc, 'line': '0x%08x' % line, 'n': n,
                       'first': ['%08x/%d=%x' % v for v in st['vals'][(pc, line)][:4]]}
                      for (pc, line), n in new]}
    with open(args.out, 'w') as f:
        json.dump(out, f, indent=1)
    print('quiet window: %d (pc, line) pairs; midi window: %d; new: %d'
          % (len(st['w']['quiet']), len(st['w']['midi']), len(new)))
    print('writers new to the MIDI window (pc: writes):')
    for pc, n in by_pc.most_common(40):
        lines = sorted({line for (p, line), _ in new if p == pc})
        print('  %08x %6d  %s%s' % (pc, n, ' '.join('%08x' % l for l in lines[:6]),
                                     ' ...' if len(lines) > 6 else ''))


if __name__ == '__main__':
    main()
