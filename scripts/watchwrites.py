#!/usr/bin/env python3
"""Log the code that writes an address range while digiemu runs: PC, value,
and the return address on the stack (who called it). For finding writers
outside the render (scripts/driven.py mismatches before a call).

    cd re/digiemu && CYCLES_FW=... uv run --no-sync python ../../scripts/watchwrites.py LO HI [INSTRS]
    ... watchwrites.py --call FUNC [INSTRS]   (who calls FUNC: return address, args, *arg0)
"""
import collections
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import emuhost  # noqa: E402
from unicorn import UC_HOOK_MEM_WRITE, UC_HOOK_CODE  # noqa: E402
from unicorn.m68k_const import UC_M68K_REG_PC, UC_M68K_REG_A7  # noqa: E402


def calls(func, n):
    out = collections.Counter()
    shown = []

    def on_code(uc, address, size, _):
        sp = uc.reg_read(UC_M68K_REG_A7)
        w = [int.from_bytes(uc.mem_read(sp + 4 * i, 4), 'big') for i in range(4)]
        out[w[0]] += 1
        if len(shown) < 6:
            shown.append((w, bytes(uc.mem_read(w[1], 0x38)).hex()))

    def on_machine(m):
        m.uc.hook_add(UC_HOOK_CODE, on_code, begin=func, end=func)
    emu = emuhost.start(on_machine)
    t0 = emu.stats['instrs']
    while emu.stats['instrs'] - t0 < n:
        time.sleep(0.2)
    emuhost.stop(emu)
    for ret, cnt in sorted(out.items()):
        print('called from %08x x%d' % (ret, cnt))
    for w, blk in shown:
        print('  ret %08x args %s  *arg0 %s' % (w[0], ' '.join('%08x' % x for x in w[1:]), blk))


def main():
    if sys.argv[1] == '--call':
        return calls(int(sys.argv[2], 16), emuhost.parse_n(sys.argv[3]) if len(sys.argv) > 3 else 40_000_000)
    lo, hi = int(sys.argv[1], 16), int(sys.argv[2], 16)
    n = emuhost.parse_n(sys.argv[3]) if len(sys.argv) > 3 else emuhost.parse_n('40M')
    seen = collections.Counter()
    first = {}

    def on_write(uc, access, addr, size, value, _):
        pc = uc.reg_read(UC_M68K_REG_PC)
        sp = uc.reg_read(UC_M68K_REG_A7)
        ret = int.from_bytes(uc.mem_read(sp, 4), 'big')
        key = (pc, addr & ~3)
        seen[key] += 1
        first.setdefault(key, (size, value, ret))

    def on_machine(m):
        m.uc.hook_add(UC_HOOK_MEM_WRITE, on_write, begin=lo, end=hi)
    emu = emuhost.start(on_machine)
    t0 = emu.stats['instrs']
    while emu.stats['instrs'] - t0 < n:
        time.sleep(0.2)
    emuhost.stop(emu)
    for (pc, a), cnt in sorted(seen.items()):
        size, value, ret = first[(pc, a)]
        print('pc %08x writes %08x (%d bytes, first %x) x%d   [sp] %08x' % (pc, a, size, value, cnt, ret))


if __name__ == '__main__':
    main()
