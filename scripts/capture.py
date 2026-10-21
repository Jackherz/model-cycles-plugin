#!/usr/bin/env python3
"""Capture consecutive calls of the Model:Cycles block render from digiemu,
for scripts/replay.py to re-run standalone and compare bit for bit.

At the first entry to the render ISR (FUN_40058c5e) after the setup, this
saves every mapped region and the CPU context. For each of the next --calls
calls it then records:

  entry   the CPU context (unicorn context_save: EMAC included) and every
          page in the render's working set that changed since the previous
          call's exit (other code -- tasks, MIDI, DMA -- writes between calls)
  mmio    every read the render made from a peripheral window, with the
          value it got, in order (replay feeds them back)
  exit    the working-set pages that changed during the call, and the
          render's peripheral writes in order (replay must match them)

The working set is the pages the render read or wrote; it grows as the run
goes; a page's entry contents are read at its first touch (the hook runs
before the access). The render's eDMA transfers (delay and reverb lines)
write memory without the hooks seeing it, so the regions DMA reaches
(DMA_REGIONS) are diffed in full at every entry and exit instead.

A call is "clean" when no interrupt was raised while the render ran (an
interrupt inside it would run code replay does not).

    cd re/digiemu && uv run --no-sync python ../../scripts/capture.py \
        --calls 300 --cc all:70:5 --retrig all:60:127 --out ../../data/cap-chord
"""
import argparse
import gzip
import json
import os
import pickle
import sys
import threading
import time

# Exact PCs in memory hooks (digiemu skips NO_HOOK_PC_SYNC with this set);
# it changes no guest behaviour.
os.environ['DIGIKIT_PIT3_PROBE'] = '1'
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import emuhost  # noqa: E402

from unicorn import (UC_HOOK_CODE, UC_HOOK_BLOCK, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE,  # noqa: E402
                     UC_MEM_WRITE)
from unicorn.m68k_const import (UC_M68K_REG_A7, UC_M68K_REG_D0, UC_M68K_REG_A0,  # noqa: E402
                                UC_M68K_REG_PC)

REGS = [UC_M68K_REG_D0 + i for i in range(8)] + [UC_M68K_REG_A0 + i for i in range(8)]

PAGE = 0x1000
MMIO = ((0x8c000000, 0x8c0fffff), (0xec000000, 0xec0fffff), (0xfc000000, 0xfc0fffff))


# Memory the render's eDMA transfers reach (delay/reverb lines in the
# 0x4a3-0x4a7 ring, descriptors and buffers in SRAM): DMA writes bypass the
# memory hooks, so these are diffed in full around every call.
DMA_REGIONS = ((0x4a300000, 0x4a7fffff), (0x80000000, 0x800fffff))


# Engine entry points the rest of the firmware calls between renders, logged
# with their stack arguments (and the block the first argument points at).
ENGINE_CALLS = {
    0x40059534: ('setparam', 3, 0),     # (value, track 0-5 or 6=FX, index)
    0x40059594: ('setlevel', 2, 0),     # (track, value)
    0x400594f0: ('setmute', 1, 0x1c),   # (state): mute/solo masks at +0x14/+0x18
    0x4005894a: ('trig', 1, 0x38),      # (request): immediate trig event
    0x4009203c: ('queue_timed', 2, 0),  # sequencer-timed event (should not occur stopped)
}
KIT_PTR = 0x800017e4
KIT_SIZE = 0x300
ISR_WORDS = (0x40a78e04, 12)            # vector-170 ISR: DMA position, counter, flag


def in_dma_region(addr):
    return any(lo <= addr <= hi for lo, hi in DMA_REGIONS)


def is_mmio(addr):
    return any(lo <= addr <= hi for lo, hi in MMIO)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--snapshot', default=emuhost.SNAPSHOT)
    ap.add_argument('--calls', type=int, default=300)
    ap.add_argument('--cc', action='append', default=[])
    ap.add_argument('--retrig', action='append', default=[])
    ap.add_argument('--retrig-every', type=emuhost.parse_n, default=emuhost.parse_n('4M'))
    ap.add_argument('--trace', type=int, default=0,
                    help='record PC and D0-A7 per instruction for the first N calls')
    ap.add_argument('--watch', type=lambda v: int(v, 0), action='append', default=[],
                    help='log changes to this u32 during traced calls, by step and writer')
    ap.add_argument('--stop', action='store_true',
                    help='press STOP twice first, so the sequencer is idle')
    ap.add_argument('--midi', action='append', default=[],
                    help='CALL:HEX send MIDI bytes (b0:50:0a) once CALL calls are captured')
    ap.add_argument('--out', default=os.path.join(emuhost.P, 'data', 'cap'))
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    st = {'armed': False, 'done': False, 'ret': None, 'calls': [], 'cur': None,
          'ws': set(), 'known': {}, 'grown': 0, 'irq_in_render': 0,
          'between': [], 'kit': None}
    finished = threading.Event()

    def read_page(uc, p):
        return bytes(uc.mem_read(p, PAGE))

    def diff_regions(uc, out):
        """Record every page of the DMA-reachable regions that changed."""
        for lo, hi in DMA_REGIONS:
            data = bytes(uc.mem_read(lo, hi - lo + 1))
            for off in range(0, len(data), PAGE):
                page = data[off:off + PAGE]
                if st['known'].get(lo + off) != page:
                    out[lo + off] = page
                    st['known'][lo + off] = page

    def on_entry(uc):
        if 'base' not in st:
            regions = sorted(uc.mem_regions())
            st['base'] = [(b, e, bytes(uc.mem_read(b, e - b + 1))) for b, e, _ in regions]
            for lo, hi in DMA_REGIONS:
                data = bytes(uc.mem_read(lo, hi - lo + 1))
                for off in range(0, len(data), PAGE):
                    st['known'][lo + off] = data[off:off + PAGE]
        entry = {}
        for p in st['ws']:
            data = read_page(uc, p)
            if st['known'].get(p) != data:
                entry[p] = data
                st['known'][p] = data
        diff_regions(uc, entry)
        sp = uc.reg_read(UC_M68K_REG_A7)
        st['ret'] = int.from_bytes(uc.mem_read(sp + 4, 4), 'big')
        if st['kit'] is None:
            st['kit'] = int.from_bytes(uc.mem_read(KIT_PTR, 4), 'big')
        st['cur'] = {'ctx': pickle.dumps(uc.context_save()), 'ret': st['ret'],
                     'entry': entry, 'mmio_r': [], 'mmio_w': [], 'irq': 0,
                     'touched': set(), 'actions': st['between'],
                     'isr': bytes(uc.mem_read(*ISR_WORDS)), 'sp': sp,
                     'frame': bytes(uc.mem_read(sp, 8))}
        st['between'] = []
        if len(st['calls']) < args.trace:
            st['cur']['trace'] = []

    def first_touch(uc, p):
        """A page outside the working set, before this access changes it:
        its contents now are its contents at entry (nothing in this call has
        touched it yet; DMA only reaches the diffed regions)."""
        data = read_page(uc, p)
        if st['known'].get(p) != data:
            st['cur']['entry'][p] = data
            st['known'][p] = data
        st['ws'].add(p)
        if st['calls']:
            st['grown'] += 1

    def on_exit(uc):
        cur = st['cur']
        exitd = {}
        for p in cur['touched']:
            data = read_page(uc, p)
            if st['known'].get(p) != data:
                exitd[p] = data
                st['known'][p] = data
        diff_regions(uc, exitd)
        cur['exit'] = exitd
        cur['touched'] = sorted(cur['touched'])
        st['calls'].append(cur)
        st['cur'] = None
        st['ret'] = None
        if len(st['calls']) >= args.calls:
            st['done'] = True
            finished.set()

    def block(uc, address, size, _):
        if st['done'] or not st['armed']:
            return
        if st['ret'] is None:
            if address == emuhost.RENDER_ISR:
                on_entry(uc)
        elif address == st['ret']:
            on_exit(uc)

    def mem(uc, access, address, size, value, _):
        cur = st['cur']
        if cur is None or st['ret'] is None:
            if access == UC_MEM_WRITE and 'base' in st and not st['done']:
                kit = st['kit']
                if kit is not None and kit <= address < kit + KIT_SIZE:
                    st['between'].append(('kitw', address, size,
                                          value & ((1 << 8 * size) - 1)))
                elif 0x80000000 <= address < 0x80100000:
                    st['between'].append(('sramw', address, size,
                                          value & ((1 << 8 * size) - 1),
                                          uc.reg_read(UC_M68K_REG_PC)))
            return
        if access == UC_MEM_WRITE and 'trace' in cur:
            for w in args.watch:
                if address <= w + 3 and w <= address + size - 1:
                    print('  guest write %08x/%d = %x at step %d'
                          % (address, size, value, len(cur['trace'])))
        if is_mmio(address):
            if access == UC_MEM_WRITE:
                cur['mmio_w'].append((address, size, value & ((1 << 8 * size) - 1)))
            else:
                # digiemu's peripheral read hooks were installed first and
                # have already put the register's value in memory.
                cur['mmio_r'].append((address, size,
                                      int.from_bytes(uc.mem_read(address, size), 'big')))
            return
        for p in {address & ~(PAGE - 1), (address + size - 1) & ~(PAGE - 1)}:
            if in_dma_region(p):
                continue
            if p not in st['ws']:
                first_touch(uc, p)
            cur['touched'].add(p)

    def code(uc, address, size, _):
        cur = st['cur']
        if cur is not None and st['ret'] is not None and 'trace' in cur:
            cur['trace'].append((address,) + tuple(uc.reg_read(r) for r in REGS))
            for w in args.watch:
                v = int.from_bytes(uc.mem_read(w, 4), 'big')
                last = cur.setdefault('watch', {}).get(w)
                if v != last:
                    print('watch %08x: %s -> %08x at step %d pc %08x'
                          % (w, 'start' if last is None else '%08x' % last, v,
                             len(cur['trace']) - 1, address))
                    cur['watch'][w] = v

    def engine_call(uc, address, size, _):
        if st['ret'] is not None or 'base' not in st or st['done']:
            return
        name, nargs, block_len = ENGINE_CALLS[address]
        sp = uc.reg_read(UC_M68K_REG_A7)
        args_ = [int.from_bytes(uc.mem_read(sp + 4 + 4 * i, 4), 'big') for i in range(nargs)]
        blk = bytes(uc.mem_read(args_[0], block_len)) if block_len else b''
        st['between'].append(('call', name, address, args_, blk))

    def on_machine(m):
        uc = m.uc
        for addr in ENGINE_CALLS:
            uc.hook_add(UC_HOOK_CODE, engine_call, begin=addr, end=addr)
        if args.trace:
            uc.hook_add(UC_HOOK_CODE, code)
        uc.hook_add(UC_HOOK_BLOCK, block)
        uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, mem)
        raise_vector = m.raise_vector

        def raise_and_note(vec, from_instruction=False, level=None):
            cur = st['cur']
            if cur is not None and st['ret'] is not None:
                kind = 'sync' if from_instruction else 'async'
                key = '%s %d' % (kind, vec)
                st.setdefault('irq_vecs', {}).setdefault(key, 0)
                st['irq_vecs'][key] += 1
                if not from_instruction:
                    # Deferred as if masked: the source retries after the
                    # render returns, so every call replays on its own.
                    st['irq_in_render'] += 1
                    return False
                cur['irq'] += 1
            return raise_vector(vec, from_instruction=from_instruction, level=level)
        m.raise_vector = raise_and_note

    emu = emuhost.start(on_machine, snapshot=args.snapshot)
    time.sleep(1.0)
    if args.stop:
        for _ in range(2):
            emu.inbox.append(('press', 11, 0))
            time.sleep(0.15)
            emu.inbox.append(('release', 11, 0))
            time.sleep(0.4)
    emuhost.send_ccs(emu, args.cc)
    midi_at = sorted((int(k), bytes(int(b, 16) for b in v.split(':')))
                     for k, v in (spec.split(':', 1) for spec in args.midi))
    emu.realtime = False
    emuhost.note_ons(emu, args.retrig)
    st['armed'] = True
    i0 = emu.stats['instrs']
    nxt = i0 + args.retrig_every
    last, t_last = -1, time.time()
    while not finished.wait(0.01):
        if not emu.is_alive():
            sys.exit('emulator thread died: %s' % emu.stats.get('status'))
        if emu.stats['instrs'] != last:
            last, t_last = emu.stats['instrs'], time.time()
        elif time.time() - t_last > 60:
            sys.exit('no progress for 60 s (%d calls): %s'
                     % (len(st['calls']), emu.stats.get('status')))
        while midi_at and len(st['calls']) >= midi_at[0][0]:
            emu.midi_in.put(midi_at.pop(0)[1])
        if args.retrig and emu.stats['instrs'] >= nxt:
            emuhost.note_ons(emu, args.retrig)
            nxt += args.retrig_every
    emuhost.stop(emu)

    calls = st['calls']
    meta = {'snapshot': args.snapshot, 'kit': st['kit'], 'midi': args.midi,
            'stop': args.stop, 'calls': len(calls), 'cc': args.cc,
            'retrig': args.retrig, 'render_isr': emuhost.RENDER_ISR,
            'working_set_pages': len(st['ws']),
            'pages_added_after_call_0': st['grown'],
            'dma_regions': [('0x%08x' % lo, '0x%08x' % hi) for lo, hi in DMA_REGIONS],
            'irq_in_render': st['irq_in_render'],
            'irq_vecs': st.get('irq_vecs', {}),
            'regions': [('0x%08x' % b, '0x%08x' % e) for b, e, _ in st['base']]}
    with gzip.open(os.path.join(args.out, 'base.pkl.gz'), 'wb', compresslevel=1) as f:
        pickle.dump(st['base'], f)
    with gzip.open(os.path.join(args.out, 'calls.pkl.gz'), 'wb', compresslevel=1) as f:
        pickle.dump(calls, f)
    with open(os.path.join(args.out, 'meta.json'), 'w') as f:
        json.dump(meta, f, indent=1)
    print('captured %d calls, working set %d pages (+%d after call 0) plus DMA regions, '
          '%d async irqs deferred' % (len(calls), len(st['ws']), st['grown'], st['irq_in_render']))
    print('irq vectors in render:', st.get('irq_vecs'))
    print('mmio reads/call %.1f, writes/call %.1f'
          % (sum(len(c['mmio_r']) for c in calls) / len(calls),
             sum(len(c['mmio_w']) for c in calls) / len(calls)))


if __name__ == '__main__':
    main()
