#!/usr/bin/env python3
"""Sweep one CC over 0-127 on channel 1 and report, per value, the kit word
it lands in (track 1's sound record) after the firmware has processed it.
Shows which values a parameter accepts and how it clamps or maps them.

    cd re/digiemu && uv run --no-sync python ../../scripts/ccsweep.py 105 0x1c
"""
import os
import sys
import time

os.environ['DIGIKIT_PIT3_PROBE'] = '1'
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import emuhost  # noqa: E402

from unicorn import UC_HOOK_BLOCK  # noqa: E402

KIT_PTR = 0x800017e4


def main():
    cc, off = int(sys.argv[1]), int(sys.argv[2], 0)
    wait = emuhost.parse_n(sys.argv[3]) if len(sys.argv) > 3 else emuhost.parse_n('2M')
    st = {'word': None}

    def block(uc, address, size, _):
        kit = int.from_bytes(uc.mem_read(KIT_PTR, 4), 'big')
        st['word'] = int.from_bytes(uc.mem_read(kit + 0x1c + off, 2), 'big')

    emu = emuhost.start(lambda m: m.uc.hook_add(UC_HOOK_BLOCK, block, begin=emuhost.RENDER_ISR,
                                                 end=emuhost.RENDER_ISR))
    time.sleep(1.0)
    emu.realtime = False
    res = []
    for v in range(128):
        before = st['word']
        emu.midi_in.put(bytes([0xB0, cc, v]))
        i0 = emu.stats['instrs']
        while emu.stats['instrs'] - i0 < wait:
            time.sleep(0.005)
        res.append((v, before, st['word']))
    emuhost.stop(emu)
    accepted = [(v, w) for v, b, w in res if w is not None and (w != b or w >> 8 == v)]
    print('CC %d -> record +0x%02x: %d of 128 values change or match the word' % (cc, off, len(accepted)))
    print(' '.join('%d->%d' % (v, w >> 8) for v, w in accepted))


if __name__ == '__main__':
    main()
