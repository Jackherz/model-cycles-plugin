#!/usr/bin/env python3
"""Build QEMU's m68k opcode -> handler table for the CFV4E model.

Replays register_m68k_insns() from the (patched) Unicorn QEMU source with the
CFV4E feature set, so the translator decodes exactly what Unicorn executes.
Writes aot/opcode_table.json: a list of 65536 handler names.

    python3 aot/qemu_table.py QEMU_TARGET_M68K_DIR
"""
import json
import os
import re
import sys

FEATURES = {'CF_ISA_A', 'CF_ISA_B', 'CF_ISA_APLUSC', 'BRAL', 'CF_FPU', 'CF_EMAC', 'USP'}


def main():
    src = open(os.path.join(sys.argv[1], 'translate.c')).read()
    body = src[src.index('void register_m68k_insns'):]
    body = body[:body.index('\n}\n')]
    table = ['undef'] * 65536
    pat = re.compile(r'^\s*(BASE|INSN)\((\w+),\s*([0-9a-fA-F]{4}),\s*([0-9a-fA-F]{4})(?:,\s*(\w+))?\);',
                     re.M)
    n = 0
    for kind, name, op, mask, feat in pat.findall(body):
        if kind == 'INSN' and feat not in FEATURES:
            continue
        op, mask = int(op, 16), int(mask, 16)
        # register_opcode: every opcode matching (opcode & mask) == opcode
        if op & ~mask & 0xFFFF:
            raise SystemExit('bad registration %s %04x %04x' % (name, op, mask))
        free = [b for b in range(16) if not mask & (1 << b)]
        for k in range(1 << len(free)):
            v = op
            for i, b in enumerate(free):
                if k >> i & 1:
                    v |= 1 << b
            table[v] = name
        n += 1
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'opcode_table.json')
    json.dump(table, open(out, 'w'))
    print('%d registrations, %d handlers, %d opcodes not undef'
          % (n, len(set(table)), sum(1 for t in table if t != 'undef')))


if __name__ == '__main__':
    main()
