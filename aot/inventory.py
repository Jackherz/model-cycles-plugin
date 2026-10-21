#!/usr/bin/env python3
"""Instruction inventory of the covered render code: QEMU handler names,
counts weighted by execution, and the static instruction count.

    cd re/digiemu && uv run --no-sync python ../../aot/inventory.py
"""
import collections
import json
import os
import sys

P = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(P, 're', 'digiemu'))
from emu import cfisa  # noqa: E402

BASE = 0x40000400


def main():
    img = open(os.path.join(P, 'data', 'sections', 'section_3_MAIN_OS.bin'), 'rb').read()
    table = json.load(open(os.path.join(P, 'aot', 'opcode_table.json')))
    cov = json.load(open(os.path.join(P, 'data', 'coverage.json')))
    at = cfisa.reader(img, BASE)
    static = collections.Counter()
    dyn = collections.Counter()
    nins = 0
    outside = []
    for key, (size, hits) in cov['blocks'].items():
        addr = int(key, 16)
        if not BASE <= addr < BASE + len(img):
            outside.append(key)
            continue
        pc = addr
        while pc < addr + size:
            ins = cfisa.decode(at(pc), pc)
            w = img[pc - BASE] << 8 | img[pc - BASE + 1]
            h = table[w]
            static[h] += 1
            dyn[h] += hits
            nins += 1
            pc += ins.length
    tot = sum(dyn.values())
    print('%d blocks, %d static instructions, %d handlers; blocks outside the image: %s'
          % (len(cov['blocks']), nins, len(static), outside[:10]))
    for h, n in dyn.most_common():
        print('  %-14s static %5d   dynamic %5.1f%%' % (h, static[h], 100 * n / tot))


if __name__ == '__main__':
    main()
