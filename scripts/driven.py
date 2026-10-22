#!/usr/bin/env python3
"""Drive the Model:Cycles render with nothing but its engine inputs, and
check every call against digiemu (a capture made with --stop and logged
engine calls, scripts/capture.py).

Unlike replay.py, no recorded memory is written between calls. Starting
from the base dump, before each render call this applies only:

  - the engine calls the firmware made since the previous call (setparam,
    setlevel, setmute, trig), by running the firmware's own functions in
    the bare CPU with the logged arguments, on a scratch stack
  - kit writes (the sound/FX words the render reads when a note triggers)
  - the vector-170 ISR's three words and the interrupt frame

then restores the CPU context and runs FUN_40058c5e with the recorded
peripheral reads. A call passes when the render's own effect matches
digiemu's: on every page it touched and in the DMA regions, every byte
either render changed ends with the same value. (The UI and other tasks keep
writing memory in digiemu between calls; those writes are not the
render's and are not compared.)

    cd re/digiemu && uv run --no-sync python ../../scripts/driven.py ../../data/cap-driven
"""
import argparse
import gzip
import os
import pickle
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from replay import Replay, Mismatch, PAGE, DMA_REGIONS  # noqa: E402
from unicorn.m68k_const import UC_M68K_REG_D0, UC_M68K_REG_A7  # noqa: E402

SCRATCH = 0x30000000        # unmapped in the firmware's map: our stack and blocks
SCRATCH_SIZE = 0x10000
SENTINEL = SCRATCH          # return address: emu_start stops there
ISR_WORDS = 0x40a78e04
# --synth: what the render sees at every entry, generated
SYNTH_SP = SCRATCH + 0x8000             # a stack of our own for the render
SYNTH_FRAME = bytes.fromhex('42fc2608') + (0x40058a08).to_bytes(4, 'big')  # SR 0x2608... -> idle loop
RING_HALVES = (0x4a3ed080, 0x4a3ed180)  # TCD50 SADDR: the half the DMA is playing


def synth_read(addr, size, k, phase, recorded):
    if addr == 0xfc04c010:              # INTC1 INTFRCH: the render's own force bit
        return 0x80000000
    if addr == 0xfc045640:              # TCD50 SADDR: alternates every block
        return RING_HALVES[(k + phase) & 1]
    if addr == 0xfc0453de:              # TCD30 CSR: DONE
        return 0x80
    if addr == 0xfc07000c:              # DTIM0 counter: Modded-Cycles' load governor's
        return recorded                 # clock (data-st), as digiemu's timer ran
    return 0


def call_firmware(uc, func, args, block=b''):
    """Run a firmware C function: args on the stack after a return address
    that points at the sentinel; a block argument goes in scratch memory."""
    sp = SCRATCH + SCRATCH_SIZE - 0x100
    if block:
        blk = SCRATCH + 0x1000
        uc.mem_write(blk, block)
        args = [blk] + list(args[1:])
    frame = SENTINEL.to_bytes(4, 'big') + b''.join(a.to_bytes(4, 'big') for a in args)
    uc.mem_write(sp, frame)
    uc.reg_write(UC_M68K_REG_A7, sp)
    uc.emu_start(func, SENTINEL, count=200_000)


def changes(before, after):
    return {i: after[i] for i in range(len(after)) if before[i] != after[i]}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('capture')
    ap.add_argument('--keep-going', action='store_true')
    ap.add_argument('--between', action='store_true',
                    help='also apply what the firmware did between blocks that the engine API does not: '
                         'its SRAM writes and sequencer-timed events (to account for a capture fully)')
    ap.add_argument('--synth', action='store_true',
                    help='generate the context, stack frame, ISR words and peripheral '
                         'values instead of using the recorded ones (what a plugin does)')
    args = ap.parse_args()
    with gzip.open(os.path.join(args.capture, 'base.pkl.gz')) as f:
        base = pickle.load(f)
    with gzip.open(os.path.join(args.capture, 'calls.pkl.gz')) as f:
        calls = pickle.load(f)

    r = Replay(base)
    r.uc.mem_map(SCRATCH, SCRATCH_SIZE)
    if args.synth:
        from unicorn.m68k_const import UC_M68K_REG_A7 as A7
        r.uc.context_restore(pickle.loads(calls[0]['ctx']))
        r.uc.reg_write(A7, SYNTH_SP)
        r.by_address = args.between
        synth_ctx = pickle.dumps(r.uc.context_save())
        phase = next(i for i in (0, 1) if any(
            (a, v) == (0xfc045640, RING_HALVES[i]) for a, _, v in calls[0]['mmio_r']))
        counter = int.from_bytes(calls[0]['isr'][4:8], 'big')

    def base_page(p):
        for b, e, data in base:
            if b <= p <= e:
                return data[p - b:p - b + PAGE]
        raise KeyError('%08x' % p)

    region_pages = [p for lo, hi in DMA_REGIONS for p in range(lo, hi + 1, PAGE)]
    known = {}                  # digiemu's page contents as last seen
    ok = bad = 0
    for k, c in enumerate(calls):
        for a in c['actions']:
            if a[0] == 'kitw':
                _, addr, size, value = a
                r.uc.mem_write(addr, value.to_bytes(size, 'big'))
            elif a[0] == 'sramw':
                if args.between:
                    _, addr, size, value, _pc = a
                    r.uc.mem_write(addr, value.to_bytes(size, 'big'))
            elif a[0] == 'call':
                _, name, func, fargs, blk = a
                if name == 'queue_timed' and not args.between:
                    raise SystemExit('call %d: a sequencer-timed event; capture with --stop' % k)
                if name == 'queue_timed':
                    # The firmware's clock ISR (0x40054c1e) allocates the
                    # event (pool 0x40091eb6), types it 5, then queues it;
                    # the capture saw only the queueing. The render frees
                    # type-5 events unread (jump table 0x40058da4).
                    call_firmware(r.uc, 0x40091eb6, [])
                    ev = r.uc.reg_read(UC_M68K_REG_D0)
                    if ev != fargs[0]:
                        raise SystemExit('call %d: pool gave %08x, digiemu %08x' % (k, ev, fargs[0]))
                    r.uc.mem_write(ev, (5).to_bytes(4, 'big'))
                call_firmware(r.uc, func, fargs, blk)
        c_rec = c
        if args.synth:
            isr = (0).to_bytes(4, 'big') + (counter + k).to_bytes(4, 'big') + (1).to_bytes(4, 'big')
            r.uc.mem_write(ISR_WORDS, isr)
            r.uc.mem_write(SYNTH_SP, SYNTH_FRAME)
            c = dict(c, ctx=synth_ctx, ret=0x40058a08,
                     mmio_r=[(a, sz, synth_read(a, sz, k, phase, v)) for a, sz, v in c['mmio_r']])
        else:
            r.uc.mem_write(ISR_WORDS, c['isr'])
            r.uc.mem_write(c['sp'], c['frame'])

        # digiemu before and after this render, page by page
        for p, data in c['entry'].items():
            known[p] = data
        pages = sorted(set(c['touched']) | set(region_pages))
        want_before = {p: known.get(p) or base_page(p) for p in pages}
        for p, data in c['exit'].items():
            known[p] = data
        want_after = {p: known.get(p) or base_page(p) for p in pages}
        got_before = {p: bytes(r.uc.mem_read(p, PAGE)) for p in pages}
        try:
            # entry={}: Replay.call would otherwise write digiemu's recorded
            # pages, and this test is about driving the render without them.
            r.call(dict(c, entry={}), 0x40058c5e)
            bad_pages = []
            for p in pages:
                got_after = bytes(r.uc.mem_read(p, PAGE))
                # Every byte either side's render changed must end the same
                # (dead stack below SP can differ before and match after).
                touched = set(changes(got_before[p], got_after)) | set(
                    changes(want_before[p], want_after[p]))
                if args.synth:
                    # digiemu's render pushed onto the interrupted task's
                    # stack, ours onto SYNTH_SP: dead scratch either way.
                    touched = {i for i in touched
                               if not c_rec['sp'] - 0x2000 <= p + i < c_rec['sp'] + 8}
                if any(got_after[i] != want_after[p][i] for i in touched):
                    bad_pages.append(p)
            if bad_pages:
                raise Mismatch('render effect differs on pages ' +
                               ' '.join('%08x' % p for p in bad_pages[:10]))
            ok += 1
        except Mismatch as exc:
            bad += 1
            print('call %d: MISMATCH %s' % (k, exc))
            if not args.keep_going:
                p = bad_pages[0] if 'bad_pages' in dir() and bad_pages else None
                if p is not None:
                    got_after = bytes(r.uc.mem_read(p, PAGE))
                    g = changes(got_before[p], got_after)
                    w = changes(want_before[p], want_after[p])
                    offs = sorted(i for i in set(g) | set(w) if got_after[i] != want_after[p][i])
                    print('  page %08x: %d bytes differ, first at +%03x '
                          '(replay changed %d bytes, digiemu %d)'
                          % (p, len(offs), offs[0], len(g), len(w)))
                    print('  replay before/after %s/%s  digiemu %s/%s' % (
                        got_before[p][offs[0]:offs[0] + 8].hex(), got_after[offs[0]:offs[0] + 8].hex(),
                        want_before[p][offs[0]:offs[0] + 8].hex(),
                        want_after[p][offs[0]:offs[0] + 8].hex()))
                break
    print('%d/%d calls driven bit-exact' % (ok, ok + bad))


if __name__ == '__main__':
    main()
