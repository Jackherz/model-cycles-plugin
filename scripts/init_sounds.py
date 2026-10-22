#!/usr/bin/env python3
"""Each machine's init sound, as the firmware makes it: run its init-sound
function (0x40061866: sound, unused, machine; the defaults of the parameter
descriptors for k 0..32) on a scratch record in Unicorn, from a capture's
base memory, for machines 0..N-1. Writes defaults.json for the plugin's
Reset Track (firmware-derived: it belongs in the engine state folder, never
in the repository).

    cd re/digiemu && uv run --no-sync python ../../scripts/init_sounds.py CAPTURE N OUT.json
"""
import gzip
import json
import os
import pickle
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from replay import Replay  # noqa: E402
from driven import SCRATCH, SCRATCH_SIZE, call_firmware  # noqa: E402

INIT_SOUND = 0x40061866
RECORD = SCRATCH + 0x2000     # a 100-byte sound record of our own


def main():
    cap, machines, out = sys.argv[1], int(sys.argv[2]), sys.argv[3]
    base = pickle.load(gzip.open(os.path.join(cap, 'base.pkl.gz')))
    calls = pickle.load(gzip.open(os.path.join(cap, 'calls.pkl.gz')))
    r = Replay(base)
    r.uc.mem_map(SCRATCH, SCRATCH_SIZE)
    r.uc.context_restore(pickle.loads(calls[0]['ctx']))
    sounds = {}
    for m in range(machines):
        r.uc.mem_write(RECORD, bytes(100))
        call_firmware(r.uc, INIT_SOUND, [RECORD, 0, m])
        rec = bytes(r.uc.mem_read(RECORD, 100))
        # parameter k is the 16-bit word at 0x14 + 2k; the value is its high byte
        sounds[m] = {k: rec[0x14 + 2 * k] for k in range(33)}
    json.dump({'source': 'firmware init sound 0x40061866', 'sound': sounds}, open(out, 'w'), indent=1)
    for m in range(machines):
        print(m, ' '.join('%d:%d' % (k, v) for k, v in sounds[m].items() if v))


if __name__ == '__main__':
    main()
