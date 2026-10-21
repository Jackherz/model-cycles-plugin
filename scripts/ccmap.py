#!/usr/bin/env python3
"""Map MIDI CCs onto the kit's sound-record words.

The engine's per-track sound records are at *(0x800017e4) + 0x1c + 100*track
(the active kit). For each CC this sends one value on channel 1, waits, and
lists every guest write inside the kit's records (with the writer PC), and
any other write the render later reads is out of scope here.

    cd re/digiemu && uv run --no-sync python ../../scripts/ccmap.py
"""
import argparse
import json
import os
import sys
import time

os.environ['DIGIKIT_PIT3_PROBE'] = '1'
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import emuhost  # noqa: E402

from unicorn import UC_HOOK_BLOCK, UC_HOOK_MEM_WRITE  # noqa: E402
from unicorn.m68k_const import UC_M68K_REG_PC, UC_M68K_REG_A7  # noqa: E402

KIT_PTR = 0x800017e4
KIT_SIZE = 0x300            # header, six 100-byte sound records from +0x1c, FX from +0x274


def kit_label(off):
    if off < 0x1c:
        return 'kit+0x%02x' % off
    if off < 0x274:
        rec, roff = divmod(off - 0x1c, 100)
        return 'rec%d+0x%02x' % (rec + 1, roff)
    return 'fx+0x%02x' % (off - 0x274)
# CC -> name, from https://midi.guide/d/elektron/model-cycles/
CCS = {94: 'mute', 95: 'track level', 10: 'pan', 65: 'pitch', 80: 'decay',
       16: 'color', 17: 'shape', 18: 'sweep', 19: 'contour', 12: 'delay send',
       13: 'reverb send', 7: 'volume+dist', 15: 'swing', 14: 'chance',
       70: 'machine', 66: 'punch', 67: 'gate', 102: 'lfo speed',
       103: 'lfo multiplier', 104: 'lfo fade', 105: 'lfo destination',
       106: 'lfo waveform', 107: 'lfo start phase', 108: 'lfo reset',
       109: 'lfo depth', 85: 'delay time', 86: 'delay feedback',
       87: 'reverb size', 88: 'reverb tone', 3: '(cc 3)'}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--channel', type=int, default=1)
    ap.add_argument('--values', default='37,90', help='two values sent in turn')
    ap.add_argument('--wait', type=emuhost.parse_n, default=emuhost.parse_n('8M'))
    ap.add_argument('--only', help='comma-separated CCs to probe')
    ap.add_argument('--out', default=os.path.join(emuhost.P, 'notes', 'ccmap.json'))
    args = ap.parse_args()
    vals = [int(v) for v in args.values.split(',')]

    st = {'kit': None, 'log': None, 'ret': None}

    def block(uc, address, size, _):
        if st['ret'] is None:
            if address == emuhost.RENDER_ISR:
                if st['kit'] is None:
                    st['kit'] = int.from_bytes(uc.mem_read(KIT_PTR, 4), 'big')
                sp = uc.reg_read(UC_M68K_REG_A7)
                st['ret'] = int.from_bytes(uc.mem_read(sp + 4, 4), 'big')
        elif address == st['ret']:
            st['ret'] = None

    def write(uc, access, address, size, value, _):
        kit, log = st['kit'], st['log']
        if log is None or kit is None:
            return
        if kit <= address < kit + KIT_SIZE:
            where = kit_label(address - kit)
        elif 0x80000000 <= address < 0x80100000 and st['ret'] is None:
            where = 'sram %08x' % address      # engine state, set outside the render
        else:
            return
        log.append((uc.reg_read(UC_M68K_REG_PC), where, size,
                    value & ((1 << 8 * size) - 1)))

    def on_machine(m):
        m.uc.hook_add(UC_HOOK_BLOCK, block)
        m.uc.hook_add(UC_HOOK_MEM_WRITE, write)

    emu = emuhost.start(on_machine)
    time.sleep(1.0)
    emu.realtime = False
    results = {}
    only = [int(c) for c in args.only.split(',')] if args.only else None
    for cc, name in CCS.items():
        if only is not None and cc not in only:
            continue
        if cc in (70, 94):      # machine and mute last: they change the sound
            continue
        results[cc] = probe(emu, st, args, cc, vals)
    for cc in (94, 70):
        if only is None or cc in only:
            results[cc] = probe(emu, st, args, cc, vals)
    emuhost.stop(emu)

    print('kit at %08x; sound record of track t at kit+0x1c+100*t' % st['kit'])
    table = {}
    for cc, writes in results.items():
        offs = sorted({(where, size) for _, where, size, _ in writes})
        desc = []
        for where, size in offs:
            seen = [v for _, w, s, v in writes if (w, s) == (where, size)][:4]
            pcs = sorted({'%08x' % pc for pc, w, s, _ in writes if (w, s) == (where, size)})
            desc.append('%s/%d %s by %s' % (where, size, ['%x' % v for v in seen], ','.join(pcs)))
        table[cc] = desc
        print('CC %3d %-16s %s' % (cc, CCS[cc], '\n                         '.join(desc) if desc
                                    else '(nothing)'))
    with open(args.out, 'w') as f:
        json.dump({'kit': '0x%08x' % st['kit'], 'ccs': {str(k): v for k, v in table.items()}},
                  f, indent=1)


def probe(emu, st, args, cc, vals):
    writes = []
    for v in vals:
        st['log'] = []
        emu.midi_in.put(bytes([0xB0 | (args.channel - 1), cc, v]))
        i0 = emu.stats['instrs']
        while emu.stats['instrs'] - i0 < args.wait:
            time.sleep(0.01)
        writes += st['log']
        st['log'] = None
    return writes


if __name__ == '__main__':
    main()
