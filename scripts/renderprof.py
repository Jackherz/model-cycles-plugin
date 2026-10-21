#!/usr/bin/env python3
"""Where does the Model:Cycles spend its instructions while it plays?

Runs digiemu's GUI emulator headless (stub sound card, as tools/capbench.py
does) from a snapshot, lets it settle, then installs a block-entry hook for a
fixed span of emulated instructions and counts, per executed basic block, how
often it ran and how many bytes it covers. Bytes stand in for instructions
(ColdFire instructions are 2-6 bytes); the totals are scaled to the
instruction count the emulator reports over the same span.

The hook perturbs wall time only: the emulated clocks count instructions, so
what the firmware does per emulated second is unchanged.

    cd re/digiemu && uv run --no-sync python ../../scripts/renderprof.py \
        --snapshot ../../data/resume.snap --instrs 64M --out ../../notes/prof.json

Also writes the audio rendered during the span to --wav.
"""
import argparse
import collections
import json
import os
import sys
import time
import wave

P = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIGIEMU = os.path.join(P, 're', 'digiemu')
os.environ.setdefault('DT2_SYX', os.path.join(P, 'firmware', 'model-cycles_OS1.13.syx'))
os.environ.setdefault('DT2_SECTIONS', os.path.join(P, 'data', 'sections'))
sys.path.insert(0, DIGIEMU)
sys.path.insert(0, os.path.join(DIGIEMU, 'tools'))

from unicorn import UC_HOOK_BLOCK, UC_HOOK_CODE  # noqa: E402
from unicorn.m68k_const import UC_M68K_REG_A7  # noqa: E402

VBR = 0x40000000
AUDIO_VECTOR = 170
RENDER_ISR = 0x40058c5e     # forced-interrupt block handler (NOTES.md)


def parse_n(s):
    mult = {'K': 10**3, 'M': 10**6, 'G': 10**9}
    return int(float(s[:-1]) * mult[s[-1].upper()]) if s[-1].upper() in mult else int(s, 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--snapshot', default=os.path.join(P, 'data', 'resume.snap'))
    ap.add_argument('--card', default=os.path.join(P, 'data', 'plusdrive.img'))
    ap.add_argument('--instrs', type=parse_n, default=parse_n('64M'),
                    help='emulated instructions to profile (64M = 1 emulated s)')
    ap.add_argument('--settle', type=float, default=1.0, help='wall s before profiling')
    ap.add_argument('--out', default=os.path.join(P, 'notes', 'prof.json'))
    ap.add_argument('--wav', default=os.path.join(P, 'notes', 'prof.wav'))
    ap.add_argument('--note', action='append', default=[],
                    help='CH:NOTE:VEL MIDI note-on sent when profiling starts')
    ap.add_argument('--cc', action='append', default=[],
                    help='CH:CC:VAL sent (paced) before profiling; CH may be 1-6 or "all"')
    ap.add_argument('--retrig', action='append', default=[],
                    help='CH:NOTE:VEL note-on resent every --retrig-every instructions')
    ap.add_argument('--retrig-every', type=parse_n, default=parse_n('16M'))
    ap.add_argument('--ips', type=parse_n, default=parse_n('64M'),
                    help='emulated instructions per second (device default 64M)')
    ap.add_argument('--validate', action='store_true',
                    help='also count every instruction with a code hook (slow), to check the decoder')
    ap.add_argument('--png', help='save the screen here after profiling')
    args = ap.parse_args()

    from emu import gui, midi, cfisa
    import livecheck
    gui.audioout.WaveOut = livecheck.StubOut

    on = [False]
    blocks = collections.defaultdict(lambda: [0, 0])   # addr -> [hits, size]
    info = {}

    # The render ISR runs from its entry until a block at the PC it
    # interrupted executes again (read from the exception frame at entry:
    # [A7] format/vector/SR, [A7+4] PC). Its bytes are charged per call.
    # Instructions are counted with digiemu's decoder, once per distinct
    # block: the emulator's own count includes idle spins it skips
    # (longrun fast_idle) and so tracks the clock, not the work.
    rs = {'ret': None, 'n': 0, 'calls': [], 'executed': 0}
    ninsn = {}

    def count(uc, address, size):
        data = bytes(uc.mem_read(address, size + 6))
        n, pc = 0, 0
        while pc < size:
            pc += cfisa.decode(lambda off, p=pc: data[p + off] << 8 | data[p + off + 1]).length
            n += 1
        return n

    def hook(uc, address, size, _):
        if not on[0]:
            return
        b = blocks[address]
        b[0] += 1
        b[1] = size
        n = ninsn.get(address)
        if n is None:
            n = ninsn[address] = count(uc, address, size)
        rs['executed'] += n
        if rs['ret'] is None:
            if address == RENDER_ISR:
                sp = uc.reg_read(UC_M68K_REG_A7)
                rs['ret'] = int.from_bytes(uc.mem_read(sp + 4, 4), 'big')
                rs['n'] = n
        elif address == rs['ret']:
            rs['calls'].append(rs['n'])
            rs['ret'] = None
        else:
            rs['n'] += n

    stepped = [0]

    def code_hook(uc, address, size, _):
        if on[0]:
            stepped[0] += 1

    attach = midi.MidiIn.attach

    def attach_and_hook(self, m, ips):
        info['vec170'] = int.from_bytes(m.uc.mem_read(VBR + 4 * AUDIO_VECTOR, 4), 'big')
        m.uc.hook_add(UC_HOOK_BLOCK, hook)
        if args.validate:
            m.uc.hook_add(UC_HOOK_CODE, code_hook)
        return attach(self, m, ips)
    midi.MidiIn.attach = attach_and_hook

    # MidiIn maps host time onto emulated time assuming the emulator keeps up
    # with the host; under the profiling hook it runs several times slower, so
    # every byte was scheduled into an emulated future the run never reached.
    # Offline, a byte goes in when it arrives: pin the offset to "now".
    def track_now(self, done):
        now = time.monotonic()
        self._offset, self._offset_t = done - now * self.ips, now
        return now
    midi.MidiIn._track = track_now

    emu = gui.Emulator(args.snapshot, realtime=True)
    emu.start()
    emu.ready.wait(180)
    if args.ips != parse_n('64M'):
        emu._pending_ips.append((emu.stats['instrs'], args.ips))
    time.sleep(args.settle)
    for spec in args.cc:
        ch, cc, val = spec.split(':')
        for c in (range(1, 7) if ch == 'all' else (int(ch),)):
            emu.midi_in.put(bytes([0xB0 | (c - 1), int(cc), int(val)]))
            time.sleep(0.02)
    if args.cc:
        time.sleep(1.0)                      # paced: let the firmware apply them

    def note_ons(specs):
        for spec in specs:
            ch, note, vel = spec.split(':')
            for c in (range(1, 7) if ch == 'all' else (int(ch),)):
                emu.midi_in.put(bytes([0x90 | (c - 1), int(note), int(vel)]))

    emu.realtime = False
    emu.audio_clear()
    note_ons(args.note)

    i0, f0, t0 = emu.stats['instrs'], emu.audio_frames, time.perf_counter()
    on[0] = True
    next_retrig = i0
    while emu.stats['instrs'] - i0 < args.instrs:
        if args.retrig and emu.stats['instrs'] >= next_retrig:
            note_ons(args.retrig)
            next_retrig += args.retrig_every
        time.sleep(0.01)
    on[0] = False
    if args.png and emu._panel_latch is not None:
        from emu import panel
        panel.write_png(emu._panel_latch, args.png, scale=4)
    i1, f1, t1 = emu.stats['instrs'], emu.audio_frames, time.perf_counter()
    pcm = emu.audio_take()
    emu.stop_flag.set()
    emu.join(15)

    instrs = i1 - i0
    total_bytes = sum(h * s for h, s in blocks.values()) or 1
    scale = instrs / total_bytes            # estimated instructions per byte
    regions = collections.Counter()
    for addr, (h, s) in blocks.items():
        regions[addr & ~0xFF] += h * s
    top_blocks = sorted(blocks.items(), key=lambda kv: -kv[1][0] * kv[1][1])[:200]
    calls = sorted(rs['calls'])
    render = {}
    executed = rs['executed']
    if calls:
        est = calls
        render = {'calls': len(calls), 'calls_per_s': len(calls) / (instrs / args.ips),
                  'share': sum(calls) / executed,
                  'executed_per_s': executed / (instrs / args.ips),
                  'mean_instrs': sum(est) / len(est),
                  'p50_instrs': est[len(est) // 2],
                  'p99_instrs': est[int(len(est) * 0.99)],
                  'max_instrs': est[-1]}
    out = {
        'render': render, 'cc': args.cc, 'retrig': args.retrig,
        'snapshot': args.snapshot, 'instrs': instrs, 'audio_frames': f1 - f0,
        'wall_s': t1 - t0, 'vec170_handler': info.get('vec170'),
        'distinct_blocks': len(blocks),
        'regions': [{'addr': '0x%08x' % a, 'share': b / total_bytes,
                     'est_instrs': round(b * scale)}
                    for a, b in regions.most_common(80)],
        'blocks': [{'addr': '0x%08x' % a, 'hits': h, 'size': s,
                    'share': h * s / total_bytes} for a, (h, s) in top_blocks],
    }
    with open(args.out, 'w') as f:
        json.dump(out, f, indent=1)
    if pcm:
        with wave.open(args.wav, 'wb') as w:
            w.setnchannels(2)
            w.setsampwidth(2)
            w.setframerate(48000)
            w.writeframes(bytes(pcm))

    print('instrs %d (%.2f emulated s at %.0fM/s), audio frames %d, wall %.1f s'
          % (instrs, instrs / args.ips, args.ips / 1e6, f1 - f0, t1 - t0))
    if args.validate:
        print('validate: code hook %d, decoded blocks %d (%.3f)'
              % (stepped[0], rs['executed'], rs['executed'] / max(1, stepped[0])))
    if render:
        print('executed %.1fM instr per emulated s (the rest is skipped idle)'
              % (render['executed_per_s'] / 1e6))
        print('render: %d calls (%.0f/s, 1500 expected), %.1f%% of executed; '
              'per call mean %.1fk p50 %.1fk p99 %.1fk max %.1fk instr'
              % (render['calls'], render['calls_per_s'], 100 * render['share'],
                 render['mean_instrs'] / 1e3, render['p50_instrs'] / 1e3,
                 render['p99_instrs'] / 1e3, render['max_instrs'] / 1e3))
    print('vector %d handler 0x%08x' % (AUDIO_VECTOR, info.get('vec170', 0)))
    print('top regions (256 B):')
    for r in out['regions'][:25]:
        print('  %s %5.1f%%  ~%.1fM instr' % (r['addr'], 100 * r['share'], r['est_instrs'] / 1e6))


if __name__ == '__main__':
    main()
