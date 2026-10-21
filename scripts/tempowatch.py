#!/usr/bin/env python3
"""What does a tempo change write to the engine?

Runs digiemu, opens TEMPO on the panel and turns LEVEL/DATA, and logs every
write made outside the render to engine SRAM (0x80000000-0x800fffff), with
the writer PC and the value, per step. Also reads the words that changed.

    cd re/digiemu && uv run --no-sync python ../../scripts/tempowatch.py
"""
import collections
import os
import sys
import time

os.environ['DIGIKIT_PIT3_PROBE'] = '1'
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import emuhost  # noqa: E402

from unicorn import UC_HOOK_BLOCK, UC_HOOK_MEM_WRITE  # noqa: E402
from unicorn.m68k_const import UC_M68K_REG_PC, UC_M68K_REG_A7  # noqa: E402

TEMPO_KEY, DATA_ENC = 14, 4
NOISY = {0x80001848, 0x8000184c, 0x80001850, 0x80001854}   # sequencer clock words


def main():
    st = {'ret': None, 'log': None}

    def block(uc, address, size, _):
        if st['ret'] is None:
            if address == emuhost.RENDER_ISR:
                sp = uc.reg_read(UC_M68K_REG_A7)
                st['ret'] = int.from_bytes(uc.mem_read(sp + 4, 4), 'big')
        elif address == st['ret']:
            st['ret'] = None

    def write(uc, access, address, size, value, _):
        if st['log'] is None or st['ret'] is not None:
            return
        if (0x80000000 <= address < 0x80100000 or 0x40000000 <= address < 0x48000000) \
                and (address & ~3) not in NOISY:
            st['log'].append((uc.reg_read(UC_M68K_REG_PC), address, size, value & ((1 << 8 * size) - 1)))

    def on_machine(m):
        m.uc.hook_add(UC_HOOK_BLOCK, block)
        m.uc.hook_add(UC_HOOK_MEM_WRITE, write)

    emu = emuhost.start(on_machine)
    time.sleep(1.5)

    def step(label, actions, settle=0.6):
        st['log'] = []
        for a in actions:
            emu.inbox.append(a)
            time.sleep(0.15)
        time.sleep(settle)
        log, st['log'] = st['log'], None
        if emu._panel_latch is not None:
            from emu import panel
            panel.write_png(emu._panel_latch, os.path.join(emuhost.P, 'notes', 'tempo-%s.png'
                            % label.replace(' ', '_').replace('/', '')), scale=3)
        by = collections.OrderedDict()
        for pc, addr, size, v in log:
            by.setdefault((addr, size), []).append((pc, v))
        print('== %s: %d writes' % (label, len(log)))
        items = [(k, v) for k, v in by.items() if len(v) <= 4]     # skip the busy UI/stack words
        print('   (%d addresses, %d with <= 4 writes)' % (len(by), len(items)))
        for (addr, size), ws in items[:40]:
            vals = sorted({'%x' % v for _, v in ws})
            pcs = sorted({'%08x' % pc for pc, _ in ws})
            print('   %08x/%d = %s by %s' % (addr, size, ' '.join(vals[:6]), ' '.join(pcs[:4])))

    step('press TEMPO', [('press', TEMPO_KEY, 0), ('release', TEMPO_KEY, 0)])
    for d in (+1, +1, +5, -10):
        step('LEVEL/DATA %+d' % d, [('encoder', DATA_ENC, d)])
    step('back', [('press', 12, 0), ('release', 12, 0)])
    emuhost.stop(emu)


if __name__ == '__main__':
    main()
