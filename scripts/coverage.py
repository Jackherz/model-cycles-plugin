#!/usr/bin/env python3
"""Collect the basic blocks the render (FUN_40058c5e, everything it calls)
executes, over one scenario, and merge them into a coverage file.

    cd re/digiemu && uv run --no-sync python ../../scripts/coverage.py \
        --cc all:70:5 --retrig all:60:127 --instrs 32M --out ../../data/coverage.json

Each run adds its blocks (address, size, hit count) to --out, so several
scenarios accumulate. The AOT compiler compiles these blocks and the
functions that contain them.
"""
import argparse
import json
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import emuhost  # noqa: E402

from unicorn import UC_HOOK_BLOCK  # noqa: E402
from unicorn.m68k_const import UC_M68K_REG_A7  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cc', action='append', default=[])
    ap.add_argument('--retrig', action='append', default=[])
    ap.add_argument('--retrig-every', type=emuhost.parse_n, default=emuhost.parse_n('4M'))
    ap.add_argument('--stop', action='store_true')
    ap.add_argument('--instrs', type=emuhost.parse_n, default=emuhost.parse_n('32M'))
    ap.add_argument('--label', default='')
    ap.add_argument('--out', default=os.path.join(emuhost.P, 'data', 'coverage.json'))
    args = ap.parse_args()

    st = {'ret': None, 'on': False, 'blocks': {}, 'calls': 0}

    def block(uc, address, size, _):
        if not st['on']:
            return
        if st['ret'] is None:
            if address != emuhost.RENDER_ISR:
                return
            sp = uc.reg_read(UC_M68K_REG_A7)
            st['ret'] = int.from_bytes(uc.mem_read(sp + 4, 4), 'big')
            st['calls'] += 1
        elif address == st['ret']:
            st['ret'] = None
            return
        b = st['blocks'].get(address)
        if b is None:
            st['blocks'][address] = [size, 1]
        else:
            b[1] += 1
            b[0] = max(b[0], size)

    emu = emuhost.start(lambda m: m.uc.hook_add(UC_HOOK_BLOCK, block))
    time.sleep(1.0)
    if args.stop:
        for _ in range(2):
            emu.inbox.append(('press', 11, 0))
            time.sleep(0.15)
            emu.inbox.append(('release', 11, 0))
            time.sleep(0.4)
    emuhost.send_ccs(emu, args.cc)
    emu.realtime = False
    emuhost.note_ons(emu, args.retrig)
    st['on'] = True
    i0 = emu.stats['instrs']
    nxt = i0 + args.retrig_every
    while emu.stats['instrs'] - i0 < args.instrs:
        if args.retrig and emu.stats['instrs'] >= nxt:
            emuhost.note_ons(emu, args.retrig)
            nxt += args.retrig_every
        time.sleep(0.01)
    st['on'] = False
    emuhost.stop(emu)

    cov = {'blocks': {}, 'runs': []}
    if os.path.exists(args.out):
        with open(args.out) as f:
            cov = json.load(f)
    new = 0
    for addr, (size, hits) in st['blocks'].items():
        key = '0x%08x' % addr
        old = cov['blocks'].get(key)
        if old is None:
            new += 1
            cov['blocks'][key] = [size, hits]
        else:
            cov['blocks'][key] = [max(old[0], size), old[1] + hits]
    cov['runs'].append({'label': args.label, 'cc': args.cc, 'retrig': args.retrig,
                        'stop': args.stop, 'calls': st['calls'], 'blocks': len(st['blocks']),
                        'new': new})
    with open(args.out, 'w') as f:
        json.dump(cov, f, indent=0)
    print('%s: %d render calls, %d blocks, %d new, %d total'
          % (args.label, st['calls'], len(st['blocks']), new, len(cov['blocks'])))


if __name__ == '__main__':
    main()
