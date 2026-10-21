#!/usr/bin/env python3
"""Write the engine's starting state for aot/engine.c from a capture made
with --stop: every mapped region (regions.bin: address, length, bytes) and
the render's CPU context plus the ISR counter and ring phase (cpu.bin).

    cd re/digiemu && uv run --no-sync python ../../aot/dump_state.py ../../data/cap-driven ../../data/aot-state
"""
import gzip
import os
import pickle
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_replay import Aot  # noqa: E402

RING_HALVES = (0x4a3ed080, 0x4a3ed180)


def main():
    cap, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    base = pickle.load(gzip.open(os.path.join(cap, 'base.pkl.gz')))
    calls = pickle.load(gzip.open(os.path.join(cap, 'calls.pkl.gz')))
    with open(os.path.join(out, 'regions.bin'), 'wb') as f:
        for b, e, data in base:
            f.write(struct.pack('<II', b, len(data)))
            f.write(data)
    cpu = Aot().cpu_from_context(calls[0]['ctx'])
    phase = next(i for i in (0, 1) if any((a, v) == (0xfc045640, RING_HALVES[i])
                                          for a, _, v in calls[0]['mmio_r']))
    counter = int.from_bytes(calls[0]['isr'][4:8], 'big')
    with open(os.path.join(out, 'cpu.bin'), 'wb') as f:
        f.write(bytes(cpu))
        f.write(struct.pack('<Ii', counter, phase))
    print('wrote %s: %d regions, %.1f MB' % (out, len(base), sum(len(d) for _, _, d in base) / 1e6))


if __name__ == '__main__':
    main()
