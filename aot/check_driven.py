#!/usr/bin/env python3
"""The plugin's loop on the AOT render, checked against digiemu: what
scripts/driven.py --synth does, with aot/build/libmcrender.dylib in place of
Unicorn.

From the base dump only, before each block: the engine calls the firmware
made (setparam, setlevel, setmute, trig) run as compiled code on a scratch
stack, kit writes are applied, the vector-170 words and an exception frame
on our own stack are written, and the peripheral values are generated. Then
the render runs, and its effect (every byte it changed, on every page it
touched and in the DMA regions) must equal digiemu's.

    cd re/digiemu && uv run --no-sync python ../../aot/check_driven.py ../../data/cap-driven
"""
import argparse
import gzip
import os
import pickle
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_replay import Aot, Cpu, PAGE, DMA_REGIONS, RENDER_ISR, STATUS  # noqa: E402

SCRATCH = 0x30000000
SENTINEL = SCRATCH                       # return address of engine calls
CALL_SP = SCRATCH + 0x7000
RENDER_SP = SCRATCH + 0x8000
FRAME = bytes.fromhex('42fc2608') + (0x40058a08).to_bytes(4, 'big')
FRAME_PC = 0x40058a08
ISR_WORDS = 0x40a78e04
RING_HALVES = (0x4a3ed080, 0x4a3ed180)


def changes(before, after):
    return {i: after[i] for i in range(len(after)) if before[i] != after[i]}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('capture')
    ap.add_argument('--keep-going', action='store_true')
    args = ap.parse_args()
    with gzip.open(os.path.join(args.capture, 'base.pkl.gz')) as f:
        base = pickle.load(f)
    with gzip.open(os.path.join(args.capture, 'calls.pkl.gz')) as f:
        calls = pickle.load(f)

    aot = Aot()
    for b, e, data in base:
        aot.write(b, data)
    template = aot.cpu_from_context(calls[0]['ctx'])
    phase = next(i for i in (0, 1) if any((a, v) == (0xfc045640, RING_HALVES[i])
                                          for a, _, v in calls[0]['mmio_r']))
    counter = int.from_bytes(calls[0]['isr'][4:8], 'big')

    def base_page(p):
        for b, e, data in base:
            if b <= p <= e:
                return data[p - b:p - b + PAGE]
        raise KeyError('%08x' % p)

    region_pages = [p for lo, hi in DMA_REGIONS for p in range(lo, hi + 1, PAGE)]
    known = {}
    ok = bad = 0
    t_calls = t_render = 0.0
    for k, c in enumerate(calls):
        for a in c['actions']:
            if a[0] == 'kitw':
                aot.write(a[1], a[3].to_bytes(a[2], 'big'))
            elif a[0] == 'call':
                _, name, func, fargs, blk = a
                if blk:
                    aot.write(SCRATCH + 0x1000, blk)
                    fargs = [SCRATCH + 0x1000] + list(fargs[1:])
                aot.write(CALL_SP, b''.join(v.to_bytes(4, 'big') for v in [SENTINEL] + list(fargs)))
                cpu = Cpu.from_buffer_copy(template)
                cpu.a[7] = CALL_SP
                t0 = time.perf_counter()
                st, _ = aot.run(cpu, func, SENTINEL)
                t_calls += time.perf_counter() - t0
                if st != 0:
                    raise SystemExit('call %d: engine call %s failed: %s at %08x'
                                     % (k, name, STATUS.get(st, st), cpu.pc))
        aot.write(ISR_WORDS, (0).to_bytes(4, 'big') + (counter + k).to_bytes(4, 'big') +
                  (1).to_bytes(4, 'big'))
        aot.write(RENDER_SP, FRAME)
        aot.write(0xfc04c010, (0x80000000).to_bytes(4, 'big'))
        aot.write(0xfc045640, RING_HALVES[(k + phase) & 1].to_bytes(4, 'big'))
        aot.write(0xfc050014, bytes(4))

        for p, data in c['entry'].items():
            known[p] = data
        pages = sorted(set(c['touched']) | set(region_pages))
        want_before = {p: known.get(p) or base_page(p) for p in pages}
        for p, data in c['exit'].items():
            known[p] = data
        want_after = {p: known.get(p) or base_page(p) for p in pages}
        got_before = {p: aot.read(p, PAGE) for p in pages}

        cpu = Cpu.from_buffer_copy(template)
        cpu.a[7] = RENDER_SP
        t0 = time.perf_counter()
        st, writes = aot.run(cpu, RENDER_ISR, FRAME_PC)
        t_render += time.perf_counter() - t0
        problems = []
        if st != 0:
            problems.append('status %s at %08x' % (STATUS.get(st, st), cpu.pc))
        if writes != c['mmio_w']:
            problems.append('peripheral writes differ (%d, digiemu %d)' % (len(writes), len(c['mmio_w'])))
            import difflib
            for line in difflib.unified_diff(['%08x/%d=%x' % w for w in c['mmio_w']],
                                             ['%08x/%d=%x' % w for w in writes], 'digiemu', 'aot', n=1, lineterm=''):
                print('   ', line)
        bad_pages = []
        for p in pages:
            got_after = aot.read(p, PAGE)
            touched = set(changes(got_before[p], got_after)) | set(changes(want_before[p], want_after[p]))
            touched = {i for i in touched if not c['sp'] - 0x2000 <= p + i < c['sp'] + 8}
            if any(got_after[i] != want_after[p][i] for i in touched):
                bad_pages.append(p)
        if bad_pages:
            problems.append('render effect differs on ' + ' '.join('%08x' % p for p in bad_pages[:8]))
        if problems:
            bad += 1
            print('call %d: %s' % (k, '; '.join(problems)))
            if not args.keep_going:
                break
        else:
            ok += 1
    n = max(1, ok + bad)
    print('%d/%d blocks driven bit-exact on the AOT render; %.3f ms per render, %.3f ms of engine calls per block'
          % (ok, ok + bad, 1e3 * t_render / n, 1e3 * t_calls / n))


if __name__ == '__main__':
    main()
