#!/usr/bin/env python3
"""Ahead-of-time translator: the Model:Cycles render (ColdFire V4e + EMAC)
to one C function.

Semantics follow QEMU's target/m68k/translate.c in the patched Unicorn that
digiemu runs (the reference the result is checked against); each emitter
below names the DISAS_INSN it mirrors. The decode table is QEMU's own, built
for the CFV4E feature set by aot/qemu_table.py.

Code: every block in data/coverage.json, plus whatever is statically
reachable from them (branch and call targets, fall-through). One C function,
one label per block start. Registers, flags (QEMU's flushed CC_OP_FLAGS
form: X and C 0/1, N and V by sign, Z zero when set) and the EMAC state are
C locals, so the compiler drops flag work nobody reads. Calls push the real
return address and a shadow (address, label) pair; RTS returns through the
shadow label when it matches, else through the dispatch switch.

    python3 aot/translate.py            # -> aot/build/render.c
"""
import json
import os
import struct
import sys

P = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMG_BASE = 0x40000400
OS_BYTE, OS_WORD, OS_LONG, OS_UNSIZED = 0, 1, 2, -1
SIZE_BYTES = {OS_BYTE: 1, OS_WORD: 2, OS_LONG: 4}
MMIO_BASE = 0x8C000000


class Unsupported(Exception):
    pass


def s8(v):
    return v - 0x100 if v & 0x80 else v


def s16(v):
    return v - 0x10000 if v & 0x8000 else v


def c(v):
    """C literal or expression."""
    if isinstance(v, int):
        return '0x%08xu' % (v & 0xFFFFFFFF)
    return v


def ext(v, size, sign):
    if size == OS_LONG:
        return c(v)
    if isinstance(v, int):
        if size == OS_BYTE:
            v &= 0xFF
            return (v - 0x100 if (sign and v & 0x80) else v) & 0xFFFFFFFF
        v &= 0xFFFF
        return (v - 0x10000 if (sign and v & 0x8000) else v) & 0xFFFFFFFF
    t = {(OS_BYTE, 1): 'int8_t', (OS_BYTE, 0): 'uint8_t',
         (OS_WORD, 1): 'int16_t', (OS_WORD, 0): 'uint16_t'}[(size, 1 if sign else 0)]
    return '(uint32_t)(%s)(%s)' % (t, v)


def insn_opsize(insn):
    return {0: OS_BYTE, 1: OS_WORD, 2: OS_LONG}.get((insn >> 6) & 3, OS_BYTE)


class Insn:
    """Emits one instruction: QEMU's DisasContext for it."""

    def __init__(self, tr, pc):
        self.tr, self.addr, self.pc = tr, pc, pc
        self.out = []
        self.ntmp = 0
        self.pending = {}          # areg writebacks (delay_set_areg)
        self.flow = None           # ('fall'|'jump'|'call'|'ret'|'rte'|'bcc'|'bail', target)
        self.target = None

    # -- stream ---------------------------------------------------------
    def im16(self):
        v = self.tr.read16(self.pc)
        self.pc += 2
        return v

    def im32(self):
        hi = self.im16()
        return (hi << 16) | self.im16()

    def im8(self):
        return self.im16() & 0xFF

    # -- emit -----------------------------------------------------------
    def emit(self, s):
        self.out.append(s)

    def tmp(self, expr, ty='uint32_t'):
        if isinstance(expr, int):
            return expr
        self.ntmp += 1
        name = 't%d' % self.ntmp
        self.emit('%s %s = %s;' % (ty, name, expr))
        return name

    # -- registers --------------------------------------------------------
    def dreg(self, n):
        return 'D%d' % n

    def areg(self, n):
        return self.pending.get(n, 'A%d' % n)

    def set_areg(self, n, val):
        """tcg_gen_mov to AREG(n): the pending writeback temp if there is one."""
        self.emit('%s = %s;' % (self.areg(n), c(val)))

    def delay_set_areg(self, n, val):
        if n in self.pending:
            self.emit('%s = %s;' % (self.pending[n], c(val)))
        else:
            self.ntmp += 1
            name = 't%d' % self.ntmp
            self.emit('uint32_t %s = %s;' % (name, c(val)))
            self.pending[n] = name

    def do_writebacks(self):
        for n in sorted(self.pending):
            self.emit('A%d = %s;' % (n, self.pending[n]))
        self.pending = {}

    def partset(self, size, reg, val):
        if size == OS_BYTE:
            self.emit('%s = (%s & 0xffffff00u) | (uint8_t)(%s);' % (reg, reg, c(val)))
        elif size == OS_WORD:
            self.emit('%s = (%s & 0xffff0000u) | (uint16_t)(%s);' % (reg, reg, c(val)))
        else:
            self.emit('%s = %s;' % (reg, c(val)))

    # -- memory ---------------------------------------------------------
    def load(self, size, addr, sign):
        a = c(addr)
        if size == OS_BYTE:
            e = 'LD8(%s)' % a
        elif size == OS_WORD:
            e = 'LD16(%s)' % a
        else:
            e = 'LD32(%s)' % a
        if size != OS_LONG and sign:
            e = ext(e, size, 1)
        return self.tmp(e)

    def store(self, size, addr, val):
        n = SIZE_BYTES[size]
        if isinstance(addr, int):
            if addr >= MMIO_BASE:
                self.emit('mc_mmio_write(X, %s, %d, %s);' % (c(addr), n, c(ext(val, size, 0))))
            else:
                self.emit('ST%dR(%s, %s);' % (8 * n, c(addr), c(val)))
        else:
            self.emit('ST%d(%s, %s);' % (8 * n, addr, c(val)))

    # -- effective addresses (gen_lea_mode / gen_ea_mode) -----------------
    def lea_indexed(self, base):
        offset = self.pc
        e = self.im16()
        if not e & 0x800:            # no WORD_INDEX on ColdFire
            raise Unsupported('word index')
        if e & 0x100:                # no EXT_FULL
            raise Unsupported('full extension word')
        idx = self.areg((e >> 12) & 7) if e & 0x8000 else self.dreg((e >> 12) & 7)
        scale = (e >> 9) & 3
        add = '(%s << %d)' % (idx, scale) if scale else idx
        if base is not None:
            d = s8(e & 0xFF)
            if d:
                return self.tmp('%s + %s + %s' % (add, c(base), c(d)))
            return self.tmp('%s + %s' % (add, c(base)))
        return self.tmp('%s + %s' % (add, c((offset + s8(e & 0xFF)) & 0xFFFFFFFF)))

    def lea_mode(self, mode, reg0, size):
        if mode in (0, 1):
            return None
        if mode == 3:
            if size == OS_UNSIZED:
                return None
            return self.areg(reg0)
        if mode == 2:
            return self.areg(reg0)
        if mode == 4:
            if size == OS_UNSIZED:
                return None
            return self.tmp('%s - %d' % (self.areg(reg0), SIZE_BYTES[size]))
        if mode == 5:
            d = s16(self.im16())
            return self.tmp('%s + %s' % (self.areg(reg0), c(d)))
        if mode == 6:
            return self.lea_indexed(self.areg(reg0))
        if reg0 == 0:
            return s16(self.im16()) & 0xFFFFFFFF
        if reg0 == 1:
            return self.im32()
        if reg0 == 2:
            off = self.pc
            return (off + s16(self.im16())) & 0xFFFFFFFF
        if reg0 == 3:
            return self.lea_indexed(None)
        return None

    def lea(self, insn, size):
        return self.lea_mode((insn >> 3) & 7, insn & 7, size)

    def ldst(self, size, addr, val, what):
        if what == 'store':
            self.store(size, addr, val)
            return None
        return self.load(size, addr, what == 'loads')

    def ea_mode(self, mode, reg0, size, val, addrp, what):
        if mode == 0:
            reg = self.dreg(reg0)
            if what == 'store':
                self.partset(size, reg, val)
                return 'store'
            return ext(reg, size, what == 'loads')
        if mode == 1:
            if what == 'store':
                self.set_areg(reg0, val)
                return 'store'
            return ext(self.areg(reg0), size, what == 'loads')
        if mode == 2:
            return self.ldst(size, self.areg(reg0), val, what) or 'store'
        if mode == 3:
            reg = self.areg(reg0)
            result = self.ldst(size, reg, val, what)
            if what == 'store' or addrp is None:
                self.delay_set_areg(reg0, '%s + %d' % (reg, SIZE_BYTES[size]))
            return result or 'store'
        if mode == 4:
            if addrp is not None and what == 'store':
                tmp = addrp[0]
            else:
                tmp = self.lea_mode(mode, reg0, size)
                if tmp is None:
                    raise Unsupported('ea')
                if addrp is not None:
                    addrp[0] = tmp
            result = self.ldst(size, tmp, val, what)
            if what == 'store' or addrp is None:
                self.delay_set_areg(reg0, tmp)
            return result or 'store'
        if mode in (5, 6) or (mode == 7 and reg0 <= 3):
            if addrp is not None and what == 'store':
                tmp = addrp[0]
            else:
                tmp = self.lea_mode(mode, reg0, size)
                if tmp is None:
                    raise Unsupported('ea')
                if addrp is not None:
                    addrp[0] = tmp
            return self.ldst(size, tmp, val, what) or 'store'
        if mode == 7 and reg0 == 4:
            if what == 'store':
                raise Unsupported('store to immediate')
            if size == OS_BYTE:
                v = self.im8()
                return (s8(v) if what == 'loads' else v) & 0xFFFFFFFF
            if size == OS_WORD:
                v = self.im16()
                return (s16(v) if what == 'loads' else v) & 0xFFFFFFFF
            return self.im32()
        raise Unsupported('ea mode %d/%d' % (mode, reg0))

    def src_ea(self, insn, size, sign, addrp=None):
        return self.ea_mode((insn >> 3) & 7, insn & 7, size, None, addrp,
                            'loads' if sign else 'loadu')

    def dest_ea(self, insn, size, val, addrp=None):
        self.ea_mode((insn >> 3) & 7, insn & 7, size, val, addrp, 'store')

    # -- condition codes (CC_OP_FLAGS form) -------------------------------
    def logic_cc(self, val, size):
        self.emit('ccn = %s; ccz = ccn; ccv = 0; ccc = 0;' % c(ext(val, size, 1)))

    def add_cc(self, res, src, size):
        """set_cc_op(ADD) + gen_update_cc_add, flushed. X already set."""
        n = c(ext(res, size, 1))
        s = c(src)
        self.emit('ccn = %s; ccc = ccx; ccz = ccn; ccv = (ccn ^ %s) & ~(%s ^ %s);'
                  % (n, s, s, c(ext('ccn - %s' % s, size, 1))))

    def sub_cc(self, res, src, size):
        n = c(ext(res, size, 1))
        s = c(src)
        t0 = c(ext('ccn + %s' % s, size, 1))
        self.emit('ccn = %s; ccc = ccx; ccz = ccn; ccv = (%s ^ %s) & (ccn ^ %s);'
                  % (n, s, t0, t0))

    def cmp_cc(self, dest, src, size):
        d, s = c(dest), c(src)
        self.emit('ccc = (%s < %s); ccz = %s; ccv = (%s ^ %s) & (ccz ^ %s); ccn = ccz;'
                  % (d, s, c(ext('%s - %s' % (d, s), size, 1)), s, d, d))

    @staticmethod
    def cond(cc):
        return {
            0: '1', 1: '0',
            2: '!(ccc || ccz == 0)', 3: '(ccc || ccz == 0)',
            4: '(ccc == 0)', 5: '(ccc != 0)',
            6: '(ccz != 0)', 7: '(ccz == 0)',
            8: '((int32_t)ccv >= 0)', 9: '((int32_t)ccv < 0)',
            10: '((int32_t)ccn >= 0)', 11: '((int32_t)ccn < 0)',
            12: '((int32_t)(ccn ^ ccv) >= 0)', 13: '((int32_t)(ccn ^ ccv) < 0)',
            14: '!(ccz == 0 || (int32_t)(ccn ^ ccv) < 0)',
            15: '(ccz == 0 || (int32_t)(ccn ^ ccv) < 0)',
        }[cc]

    def get_ccr(self):
        return '(((ccx) << 4) | ((ccn >> 31) << 3) | ((ccz == 0) << 2) | ((ccv >> 31) << 1) | (ccc))'

    def set_ccr(self, v):
        v = c(v)
        self.emit('ccc = (%s & 1) ? 1 : 0; ccv = (%s & 2) ? 0xffffffffu : 0; '
                  'ccz = (%s & 4) ? 0 : 1; ccn = (%s & 8) ? 0xffffffffu : 0; ccx = (%s & 16) ? 1 : 0;'
                  % (v, v, v, v, v))

    def set_sr(self, v):
        """cpu_m68k_set_sr: SR = v & 0xffe0, CCR, stack switch (refused)."""
        t = self.tmp(c(v))
        self.emit('if (((%s ^ sr) & 0x2000u) != 0) { pc = 0x%08xu; status = MC_BAD_SR; goto out; }'
                  % (c(t), self.addr))
        self.emit('sr = %s & 0xffe0u;' % c(t))
        self.set_ccr(t)

    def push(self, val):
        sp = self.areg(7)
        t = self.tmp('%s - 4' % sp)
        self.store(OS_LONG, t, val)
        self.set_areg(7, t)


# --------------------------------------------------------------------------
# DISAS_INSN emitters. Each sets ins.flow for control transfer.

def i_move(ins, insn):
    size = {1: OS_BYTE, 2: OS_LONG, 3: OS_WORD}[insn >> 12]
    src = ins.src_ea(insn, size, 1)
    op = (insn >> 6) & 7
    if op == 1:
        ins.set_areg((insn >> 9) & 7, src)
    else:
        ins.ea_mode(op, (insn >> 9) & 7, size, src, None, 'store')
        ins.logic_cc(src, size)


def i_addsub(ins, insn):
    add = (insn & 0x4000) != 0
    size = insn_opsize(insn)
    reg = ext(ins.dreg((insn >> 9) & 7), size, 1)
    addr = [None]
    if insn & 0x100:
        tmp = ins.src_ea(insn, size, 1, addr)
        src = reg
    else:
        tmp = reg
        src = ins.src_ea(insn, size, 1)
    tmp, src = ins.tmp(c(tmp)), ins.tmp(c(src))
    if add:
        dest = ins.tmp('%s + %s' % (c(tmp), c(src)))
        ins.emit('ccx = (%s < %s);' % (c(dest), c(src)))
        ins.add_cc(dest, src, size)
    else:
        ins.emit('ccx = (%s < %s);' % (c(tmp), c(src)))
        dest = ins.tmp('%s - %s' % (c(tmp), c(src)))
        ins.sub_cc(dest, src, size)
    if insn & 0x100:
        ins.dest_ea(insn, size, dest, addr)
    else:
        ins.partset(size, ins.dreg((insn >> 9) & 7), dest)


def i_addsubq(ins, insn):
    size = OS_LONG if (insn & 0o70) == 0o10 else insn_opsize(insn)
    addr = [None]
    src = ins.src_ea(insn, size, 1, addr)
    imm = (insn >> 9) & 7 or 8
    if (insn & 0x38) == 0x08:
        dest = ins.tmp('%s %s %d' % (c(src), '-' if insn & 0x100 else '+', imm))
    else:
        src = ins.tmp(c(src))
        if insn & 0x100:
            ins.emit('ccx = (%s < %du);' % (c(src), imm))
            dest = ins.tmp('%s - %d' % (c(src), imm))
            ins.sub_cc(dest, imm, size)
        else:
            dest = ins.tmp('%s + %d' % (c(src), imm))
            ins.emit('ccx = (%s < %du);' % (c(dest), imm))
            ins.add_cc(dest, imm, size)
    ins.dest_ea(insn, size, dest, addr)


def i_arith_im(ins, insn):
    op = (insn >> 9) & 7
    size = insn_opsize(insn)
    if size == OS_BYTE:
        im = s8(ins.im8()) & 0xFFFFFFFF
    elif size == OS_WORD:
        im = s16(ins.im16()) & 0xFFFFFFFF
    else:
        im = ins.im32()
    with_sr = (insn & 0x3f) == 0x3c
    addr = [None]
    if with_sr:
        if op in (2, 3, 6) or size == OS_LONG:
            raise Unsupported('arith_im SR')
        src1 = ins.tmp(ins.get_ccr() if size == OS_BYTE else '((sr & 0xffe0u) | %s)' % ins.get_ccr())
    else:
        src1 = ins.tmp(c(ins.src_ea(insn, size, 1, None if op == 6 else addr)))
    if op in (0, 1, 5):
        dest = ins.tmp('%s %s %s' % (c(src1), {0: '|', 1: '&', 5: '^'}[op], c(im)))
        if with_sr:
            if size == OS_BYTE:
                ins.set_ccr(dest)
            else:
                ins.set_sr(dest)
        else:
            ins.dest_ea(insn, size, dest, addr)
            ins.logic_cc(dest, size)
    elif op == 2:
        ins.emit('ccx = (%s < %s);' % (c(src1), c(im)))
        dest = ins.tmp('%s - %s' % (c(src1), c(im)))
        ins.sub_cc(dest, im, size)
        ins.dest_ea(insn, size, dest, addr)
    elif op == 3:
        dest = ins.tmp('%s + %s' % (c(src1), c(im)))
        ins.emit('ccx = (%s < %s);' % (c(dest), c(im)))
        ins.add_cc(dest, im, size)
        ins.dest_ea(insn, size, dest, addr)
    elif op == 6:
        ins.cmp_cc(src1, im, size)
    else:
        raise Unsupported('arith_im op %d' % op)


def i_adda(ins, insn):
    src = ins.src_ea(insn, OS_LONG if insn & 0x100 else OS_WORD, 1)
    r = ins.areg((insn >> 9) & 7)
    ins.emit('%s = %s + %s;' % (r, r, c(src)))


def i_suba(ins, insn):
    src = ins.src_ea(insn, OS_LONG if insn & 0x100 else OS_WORD, 1)
    r = ins.areg((insn >> 9) & 7)
    ins.emit('%s = %s - %s;' % (r, r, c(src)))


def i_shift_im(ins, insn):
    count = (insn >> 9) & 7 or 8
    logical = insn & 8
    left = insn & 0x100
    reg = ins.dreg(insn & 7)
    if left:
        ins.emit('ccc = (%s >> %d) & 1; ccn = %s << %d;' % (reg, 32 - count, reg, count))
    else:
        ins.emit('ccc = (%s >> %d) & 1; ccn = %s;' % (
            reg, count - 1,
            '%s >> %d' % (reg, count) if logical else '(uint32_t)((int32_t)%s >> %d)' % (reg, count)))
    ins.emit('ccv = 0; ccz = ccn; ccx = ccc; %s = ccn;' % reg)


def i_shift_reg(ins, insn):
    logical = insn & 8
    left = insn & 0x100
    reg = ins.dreg(insn & 7)
    s32 = ins.tmp('%s & 63' % ins.dreg((insn >> 9) & 7))
    if left:
        t = ins.tmp('(uint64_t)%s << %s' % (reg, s32), 'uint64_t')
        ins.emit('ccn = (uint32_t)%s; ccc = (uint32_t)(%s >> 32) & 1;' % (t, t))
    else:
        if logical:
            t = ins.tmp('((uint64_t)%s << 32) >> %s' % (reg, s32), 'uint64_t')
        else:
            t = ins.tmp('(uint64_t)(((int64_t)(int32_t)%s * 4294967296ll) >> %s)' % (reg, s32),
                        'uint64_t')
        ins.emit('ccc = ((uint32_t)%s) >> 31; ccn = (uint32_t)(%s >> 32);' % (t, t))
    ins.emit('ccv = 0; if (%s != 0) ccx = ccc; ccz = ccn; %s = ccn;' % (s32, reg))


def i_moveq(ins, insn):
    r = ins.dreg((insn >> 9) & 7)
    ins.emit('%s = %s;' % (r, c(s8(insn & 0xFF) & 0xFFFFFFFF)))
    ins.logic_cc(r, OS_LONG)


def i_lea(ins, insn):
    t = ins.lea(insn, OS_LONG)
    if t is None:
        raise Unsupported('lea')
    ins.set_areg((insn >> 9) & 7, t)


def i_pea(ins, insn):
    t = ins.lea(insn, OS_LONG)
    if t is None:
        raise Unsupported('pea')
    ins.push(t)


def i_eor(ins, insn):
    size = insn_opsize(insn)
    addr = [None]
    src = ins.src_ea(insn, size, 0, addr)
    dest = ins.tmp('%s ^ %s' % (c(src), ins.dreg((insn >> 9) & 7)))
    ins.logic_cc(dest, size)
    ins.dest_ea(insn, size, dest, addr)


def i_and(ins, insn):
    size = insn_opsize(insn)
    reg = ins.dreg((insn >> 9) & 7)
    if insn & 0x100:
        addr = [None]
        src = ins.src_ea(insn, size, 0, addr)
        dest = ins.tmp('%s & %s' % (c(src), reg))
        ins.dest_ea(insn, size, dest, addr)
    else:
        src = ins.src_ea(insn, size, 0)
        dest = ins.tmp('%s & %s' % (c(src), reg))
        ins.partset(size, reg, dest)
    ins.logic_cc(dest, size)


def i_or(ins, insn):
    size = insn_opsize(insn)
    reg = ins.tmp(c(ext(ins.dreg((insn >> 9) & 7), size, 0)))
    if insn & 0x100:
        addr = [None]
        src = ins.src_ea(insn, size, 0, addr)
        dest = ins.tmp('%s | %s' % (c(src), c(reg)))
        ins.dest_ea(insn, size, dest, addr)
    else:
        src = ins.src_ea(insn, size, 0)
        dest = ins.tmp('%s | %s' % (c(src), c(reg)))
        ins.partset(size, ins.dreg((insn >> 9) & 7), dest)
    ins.logic_cc(dest, size)


def i_not(ins, insn):
    size = insn_opsize(insn)
    addr = [None]
    src = ins.src_ea(insn, size, 1, addr)
    dest = ins.tmp('~%s' % c(src))
    ins.dest_ea(insn, size, dest, addr)
    ins.logic_cc(dest, size)


def i_neg(ins, insn):
    size = insn_opsize(insn)
    addr = [None]
    src1 = ins.tmp(c(ins.src_ea(insn, size, 1, addr)))
    dest = ins.tmp('0u - %s' % c(src1))
    ins.emit('ccx = (%s != 0);' % c(dest))      # C is X at flush time
    ins.sub_cc(dest, src1, size)
    ins.dest_ea(insn, size, dest, addr)


def i_cmp(ins, insn):
    size = insn_opsize(insn)
    src = ins.tmp(c(ins.src_ea(insn, size, 1)))
    reg = ins.tmp(c(ext(ins.dreg((insn >> 9) & 7), size, 1)))
    ins.cmp_cc(reg, src, size)


def i_cmpa(ins, insn):
    size = OS_LONG if insn & 0x100 else OS_WORD
    src = ins.tmp(c(ins.src_ea(insn, size, 1)))
    ins.cmp_cc(ins.areg((insn >> 9) & 7), src, OS_LONG)


def i_tst(ins, insn):
    size = insn_opsize(insn)
    ins.logic_cc(ins.src_ea(insn, size, 1), size)


def i_clr(ins, insn):
    size = insn_opsize(insn)
    ins.dest_ea(insn, size, 0)
    ins.logic_cc(0, size)


def i_sats(ins, insn):
    r = ins.dreg(insn & 7)
    ins.emit('%s = mc_sats(%s, ccv);' % (r, r))
    ins.logic_cc(r, OS_LONG)


def i_mvzs(ins, insn):
    size = OS_WORD if insn & 0x40 else OS_BYTE
    src = ins.tmp(c(ins.src_ea(insn, size, (insn & 0x80) == 0)))
    ins.emit('%s = %s;' % (ins.dreg((insn >> 9) & 7), c(src)))
    ins.logic_cc(src, size)


def i_swap(ins, insn):
    r = ins.dreg(insn & 7)
    ins.emit('%s = (%s << 16) | (%s >> 16);' % (r, r, r))
    ins.logic_cc(r, OS_LONG)


def i_ext(ins, insn):
    r = ins.dreg(insn & 7)
    op = (insn >> 6) & 7
    t = ins.tmp('(uint32_t)(int16_t)%s' % r if op == 3 else '(uint32_t)(int8_t)%s' % r)
    if op == 2:
        ins.partset(OS_WORD, r, t)
    else:
        ins.emit('%s = %s;' % (r, t))
    ins.logic_cc(t, OS_LONG)


def i_ff1(ins, insn):
    r = ins.dreg(insn & 7)
    ins.logic_cc(r, OS_LONG)
    ins.emit('%s = mc_ff1(%s);' % (r, r))


def i_subx_reg(ins, insn):
    size = insn_opsize(insn)
    src = ins.tmp(c(ext(ins.dreg(insn & 7), size, 1)))
    dest = ins.tmp(c(ext(ins.dreg((insn >> 9) & 7), size, 1)))
    r = ins.tmp('(uint64_t)%s - ((uint64_t)%s + ccx)' % (c(dest), c(src)), 'uint64_t')
    ins.emit('ccn = %s; ccx = (uint32_t)(%s >> 32) & 1;' % (c(ext('(uint32_t)%s' % r, size, 1)), r))
    ins.emit('ccv = (ccn ^ %s) & (%s ^ %s); ccz |= ccn; ccc = ccx;' % (c(dest), c(dest), c(src)))
    ins.partset(size, ins.dreg((insn >> 9) & 7), 'ccn')


def i_addx_reg(ins, insn):
    size = insn_opsize(insn)
    src = ins.tmp(c(ext(ins.dreg(insn & 7), size, 1)))
    dest = ins.tmp(c(ext(ins.dreg((insn >> 9) & 7), size, 1)))
    r = ins.tmp('(uint64_t)%s + %s + ccx' % (c(dest), c(src)), 'uint64_t')
    ins.emit('ccn = %s; ccx = (uint32_t)(%s >> 32) & 1;' % (c(ext('(uint32_t)%s' % r, size, 1)), r))
    ins.emit('ccv = (ccn ^ %s) & ~(%s ^ %s); ccz |= ccn; ccc = ccx;' % (c(src), c(dest), c(src)))
    ins.partset(size, ins.dreg((insn >> 9) & 7), 'ccn')


def i_byterev(ins, insn):
    r = ins.dreg(insn & 7)
    ins.emit('%s = __builtin_bswap32(%s);' % (r, r))


def i_mull(ins, insn):
    e = ins.im16()
    if e & 0x400:
        raise Unsupported('mull 64')
    src1 = ins.src_ea(insn, OS_LONG, 0)
    r = ins.dreg((e >> 12) & 7)
    ins.emit('%s = %s * %s;' % (r, c(src1), r))
    ins.logic_cc(r, OS_LONG)


def i_mulw(ins, insn):
    sign = (insn & 0x100) != 0
    r = ins.dreg((insn >> 9) & 7)
    t = ins.tmp(c(ext(r, OS_WORD, sign)))
    src = ins.src_ea(insn, OS_WORD, sign)
    ins.emit('%s = %s * %s;' % (t, t, c(src)))
    ins.emit('%s = %s;' % (r, t))
    ins.logic_cc(t, OS_LONG)


def i_divl(ins, insn):
    e = ins.im16()
    if e & 0x400:
        raise Unsupported('divl 64')
    sign = (e & 0x800) != 0
    den = ins.tmp(c(ins.src_ea(insn, OS_LONG, 0)))
    numr, regr = (e >> 12) & 7, e & 7
    ins.emit('if (%s == 0) { pc = 0x%08xu; status = MC_FAULT; goto out; }' % (c(den), ins.addr))
    num = ins.dreg(numr)
    if sign:
        ins.emit('uint32_t q, r; if ((int32_t)%s == INT32_MIN && (int32_t)%s == -1) { q = %s; r = 0; } '
                 'else { q = (uint32_t)((int32_t)%s / (int32_t)%s); r = (uint32_t)((int32_t)%s %% (int32_t)%s); }'
                 % (num, c(den), num, num, c(den), num, c(den)))
    else:
        ins.emit('uint32_t q = %s / %s, r = %s %% %s;' % (num, c(den), num, c(den)))
    ins.emit('ccc = 0; ccz = q; ccn = q; ccv = 0;')
    if numr == regr:
        ins.emit('%s = q;' % ins.dreg(numr))
    else:
        ins.emit('%s = r;' % ins.dreg(regr))


def bitop(ins, insn, mask_expr, size):
    op = (insn >> 6) & 3
    addr = [None]
    src1 = ins.tmp(c(ins.src_ea(insn, size, 0, addr if op else None)))
    ins.emit('ccz = %s & %s;' % (c(src1), mask_expr))
    if op:
        e = {1: '%s ^ %s', 2: '%s & ~%s', 3: '%s | %s'}[op] % (c(src1), mask_expr)
        ins.dest_ea(insn, size, ins.tmp(e), addr)


def i_bitop_reg(ins, insn):
    size = OS_BYTE if insn & 0x38 else OS_LONG
    m = ins.tmp('1u << (%s & %d)' % (ins.dreg((insn >> 9) & 7), 7 if size == OS_BYTE else 31))
    bitop(ins, insn, m, size)


def i_bitop_im(ins, insn):
    size = OS_BYTE if insn & 0x38 else OS_LONG
    bit = ins.im16()
    if bit & 0xff00:
        raise Unsupported('bitop_im undef')
    bit &= 7 if size == OS_BYTE else 31
    bitop(ins, insn, c(1 << bit), size)


def i_scc(ins, insn):
    t = ins.tmp('0u - (uint32_t)(%s)' % Insn.cond((insn >> 8) & 0xf))
    ins.dest_ea(insn, OS_BYTE, t)


def i_movem(ins, insn):
    is_load = (insn & 0x400) != 0
    size = OS_LONG if insn & 0x40 else OS_WORD
    mask = ins.im16()
    mode, reg0 = (insn >> 3) & 7, insn & 7
    if mode in (0, 1) or (mode == 3 and not is_load) or (mode == 4 and is_load):
        raise Unsupported('movem mode')
    if mode in (2, 3, 4):
        tmp = 'A%d' % reg0
    else:
        tmp = ins.lea_mode(mode, reg0, size)
        if tmp is None:
            raise Unsupported('movem ea')
    ins.ntmp += 1
    addr = 'm%d' % ins.ntmp
    ins.emit('uint32_t %s = %s;' % (addr, c(tmp)))
    n = SIZE_BYTES[size]

    def mreg(i):
        return 'D%d' % i if i < 8 else 'A%d' % (i - 8)
    if is_load:
        regs = []
        for i in range(16):
            if mask & (1 << i):
                v = ins.load(size, addr, 1)
                regs.append((i, v))
                ins.emit('%s += %d;' % (addr, n))
        for i, v in regs:
            ins.emit('%s = %s;' % (mreg(i), c(v)))
        if mode == 3:
            ins.emit('A%d = %s;' % (reg0, addr))
    elif mode == 4:
        for i in range(15, -1, -1):
            if (mask << i) & 0x8000:
                ins.emit('%s -= %d;' % (addr, n))
                ins.store(size, addr, mreg(i))
        ins.emit('A%d = %s;' % (reg0, addr))
    else:
        for i in range(16):
            if mask & (1 << i):
                ins.store(size, addr, mreg(i))
                ins.emit('%s += %d;' % (addr, n))


def i_link(ins, insn):
    off = s16(ins.im16())
    reg = ins.areg(insn & 7)
    t = ins.tmp('%s - 4' % ins.areg(7))
    ins.store(OS_LONG, t, reg)
    if insn & 7 != 7:
        ins.set_areg(insn & 7, t)
    ins.set_areg(7, '%s + %s' % (t, c(off)))


def i_unlk(ins, insn):
    src = ins.tmp(ins.areg(insn & 7))
    t = ins.load(OS_LONG, src, 0)
    ins.set_areg(insn & 7, t)
    ins.set_areg(7, '%s + 4' % c(src))


def i_nop(ins, insn):
    pass


def i_tpf(ins, insn):
    ins.pc += {2: 2, 3: 4}.get(insn & 7, 0)


def i_move_to_sr(ins, insn):
    if (insn & 0x3f) == 0x3c:
        ins.set_sr(ins.im16())
    else:
        ins.set_sr(ins.src_ea(insn, OS_WORD, 0))


def i_move_from_sr(ins, insn):
    ins.dest_ea(insn, OS_WORD, ins.tmp('(sr & 0xffe0u) | %s' % ins.get_ccr()))


def i_move_to_ccr(ins, insn):
    if (insn & 0x3f) == 0x3c:
        ins.set_ccr(ins.im16())
    else:
        ins.set_ccr(ins.src_ea(insn, OS_WORD, 0))


def i_move_from_ccr(ins, insn):
    ins.dest_ea(insn, OS_WORD, ins.tmp(ins.get_ccr()))


# EMAC -----------------------------------------------------------------------

def mac_word(ins, val, upper):
    return ins.tmp('mc_mac_word(macctl, %s, %d)' % (c(val), 1 if upper else 0))


def i_mac(ins, insn):
    e = ins.im16()
    acc = ((insn >> 7) & 1) | ((e >> 3) & 2)
    load = (insn & 0x30) != 0
    if load:
        tmp = ins.lea(insn, OS_LONG)
        if tmp is None:
            raise Unsupported('mac ea')
        addr = ins.tmp('%s & macmask' % c(tmp)) if e & 0x20 else ins.tmp(c(tmp))
        loadval = ins.load(OS_LONG, addr, 0)
        acc ^= 1
        rx = ins.areg((e >> 12) & 7) if e & 0x8000 else ins.dreg((e >> 12) & 7)
        ry = ins.areg(e & 7) if e & 8 else ins.dreg(e & 7)
    else:
        rx = ins.areg((insn >> 9) & 7) if insn & 0x40 else ins.dreg((insn >> 9) & 7)
        ry = ins.areg(insn & 7) if insn & 8 else ins.dreg(insn & 7)
    if (e & 0x800) == 0:
        rx = mac_word(ins, rx, e & 0x80)
        ry = mac_word(ins, ry, e & 0x40)
    # flags lazily: MAC_LAZY records what mc_mac_flags needs (dead unless read)
    ins.emit('macc[%d] = mc_mac_nf(macctl, &macpav, %d, macc[%d], %s, %s, MC_MAC_MODE(macctl), %d, %d); MAC_LAZY(%d);'
             % (acc, acc, acc, c(rx), c(ry), 1 if e & 0x100 else 0, (e >> 9) & 3, acc))
    if load:
        rw = ins.areg((insn >> 9) & 7) if insn & 0x40 else ins.dreg((insn >> 9) & 7)
        ins.emit('%s = %s;' % (rw, c(loadval)))
        mode = (insn >> 3) & 7
        if mode == 3:
            ins.emit('%s = %s + 4;' % (ins.areg(insn & 7), c(addr)))
        elif mode == 4:
            ins.emit('%s = %s;' % (ins.areg(insn & 7), c(addr)))


def i_from_mac(ins, insn):
    rx = ins.areg(insn & 7) if insn & 8 else ins.dreg(insn & 7)
    n = (insn >> 9) & 3
    ins.emit('%s = mc_from_mac(macctl, macc[%d]);' % (rx, n))
    if insn & 0x40:
        ins.emit('macc[%d] = 0; macpav &= ~(MACSR_PAV0 << %d);' % (n, n))


def i_move_mac(ins, insn):
    src, dst = insn & 3, (insn >> 9) & 3
    ins.emit('macc[%d] = macc[%d]; if (macpav & (MACSR_PAV0 << %d)) macpav |= MACSR_PAV0 << %d; '
             'else macpav &= ~(MACSR_PAV0 << %d);' % (dst, src, src, dst, dst))
    ins.emit('MAC_LAZY(%d);' % dst)


def i_from_macsr(ins, insn):
    ins.emit('MAC_FLAGS(); %s = macctl | macpav | macflg;' % (ins.areg(insn & 7) if insn & 8 else ins.dreg(insn & 7)))


def i_from_mask(ins, insn):
    ins.emit('%s = macmask;' % (ins.areg(insn & 7) if insn & 8 else ins.dreg(insn & 7)))


def i_from_mext(ins, insn):
    reg = ins.areg(insn & 7) if insn & 8 else ins.dreg(insn & 7)
    a = 2 if insn & 0x400 else 0
    ins.emit('%s = (macctl & MACSR_FI) ? mc_get_mac_extf(macc, %d) : mc_get_mac_exti(macc, %d);'
             % (reg, a, a))


def i_macsr_to_ccr(ins, insn):
    ins.emit('MAC_FLAGS();')
    ins.set_sr('(sr & 0xffe0u) | macflg')


def i_to_mac(ins, insn):
    n = (insn >> 9) & 3
    val = ins.src_ea(insn, OS_LONG, 0)
    ins.emit('macc[%d] = mc_to_mac(macctl, %s); macpav &= ~(MACSR_PAV0 << %d);' % (n, c(val), n))
    ins.emit('MAC_LAZY(%d);' % n)


def i_to_macsr(ins, insn):
    ins.emit('{ uint32_t s_ = macctl | macpav | macflg; mc_set_macsr(&s_, macc, %s); '
             'macctl = s_ & MACSR_CTL; macpav = s_ & MACSR_PAV; macflg = s_ & MACSR_FLAGS; mfl = 0; }'
             % c(ins.src_ea(insn, OS_LONG, 0)))


def i_to_mask(ins, insn):
    ins.emit('macmask = %s | 0xffff0000u;' % c(ins.src_ea(insn, OS_LONG, 0)))


def i_to_mext(ins, insn):
    val = ins.tmp(c(ins.src_ea(insn, OS_LONG, 0)))
    a = 2 if insn & 0x400 else 0
    ins.emit('if (macctl & MACSR_FI) mc_set_mac_extf(macc, %s, %d); '
             'else if (MACSR_SIGNED_INT(macctl)) mc_set_mac_exts(macc, %s, %d); '
             'else mc_set_mac_extu(macc, %s, %d);' % (c(val), a, c(val), a, c(val), a))


def i_mov3q(ins, insn):
    v = (insn >> 9) & 7
    v = 0xFFFFFFFF if v == 0 else v
    ins.dest_ea(insn, OS_LONG, v)
    ins.logic_cc(v, OS_LONG)


# control flow -----------------------------------------------------------------

def i_branch(ins, insn):
    base = ins.pc
    op = (insn >> 8) & 0xf
    off = s8(insn & 0xFF)
    if off == 0:
        off = s16(ins.im16())
    elif off == -1:
        off = ins.im32()
        off = off - (1 << 32) if off & 0x80000000 else off
    target = (base + off) & 0xFFFFFFFF
    if op == 1:
        ins.flow, ins.target = 'call', target
    elif op == 0:
        ins.flow, ins.target = 'jump', target
    else:
        ins.flow, ins.target = 'bcc', target
        ins.cc = op


def i_jump(ins, insn):
    t = ins.lea(insn, OS_LONG)
    if t is None:
        raise Unsupported('jump ea')
    ins.flow = 'call' if (insn & 0x40) == 0 else 'jump'
    ins.target = t if isinstance(t, int) else ins.tmp(c(t))   # before the push


def i_rts(ins, insn):
    ins.flow = 'ret'


def i_rte(ins, insn):
    ins.flow = 'rte'


HANDLERS = {
    'move': i_move, 'addsub': i_addsub, 'addsubq': i_addsubq, 'arith_im': i_arith_im,
    'adda': i_adda, 'suba': i_suba, 'shift_im': i_shift_im, 'shift_reg': i_shift_reg,
    'moveq': i_moveq, 'lea': i_lea, 'pea': i_pea, 'eor': i_eor, 'and': i_and, 'or': i_or,
    'not': i_not, 'neg': i_neg, 'cmp': i_cmp, 'cmpa': i_cmpa, 'tst': i_tst, 'clr': i_clr,
    'sats': i_sats, 'mvzs': i_mvzs, 'swap': i_swap, 'ext': i_ext, 'ff1': i_ff1,
    'subx_reg': i_subx_reg, 'addx_reg': i_addx_reg, 'byterev': i_byterev, 'mull': i_mull, 'mulw': i_mulw, 'divl': i_divl,
    'bitop_reg': i_bitop_reg, 'bitop_im': i_bitop_im, 'scc': i_scc, 'movem': i_movem,
    'link': i_link, 'unlk': i_unlk, 'nop': i_nop, 'tpf': i_tpf,
    'move_to_sr': i_move_to_sr, 'move_from_sr': i_move_from_sr,
    'move_to_ccr': i_move_to_ccr, 'move_from_ccr': i_move_from_ccr,
    'mac': i_mac, 'from_mac': i_from_mac, 'move_mac': i_move_mac, 'from_macsr': i_from_macsr,
    'from_mask': i_from_mask, 'from_mext': i_from_mext, 'macsr_to_ccr': i_macsr_to_ccr,
    'to_mac': i_to_mac, 'to_macsr': i_to_macsr, 'to_mask': i_to_mask, 'to_mext': i_to_mext,
    'mov3q': i_mov3q, 'branch': i_branch, 'jump': i_jump, 'rts': i_rts, 'rte': i_rte,
}


class Translator:
    hooks = set()   # PCs that call X->hook first (--hook)

    def __init__(self, img, table, extra=()):
        # code is read from the image, then from `extra` (base, bytes)
        # regions: code the firmware runs from RAM, e.g. a payload copied
        # there at boot
        self.img, self.table = img, table
        self.extra = list(extra)
        self.insns = {}            # addr -> Insn
        self.labels = set()
        self.unsupported = {}

    def read16(self, a):
        o = a - IMG_BASE
        if 0 <= o < len(self.img) - 1:
            return struct.unpack_from('>H', self.img, o)[0]
        for base, data in self.extra:
            if 0 <= a - base < len(data) - 1:
                return struct.unpack_from('>H', data, a - base)[0]
        raise Unsupported('read outside the image at %08x' % a)

    def decode(self, pc):
        insn = self.read16(pc)
        name = self.table[insn]
        ins = Insn(self, pc)
        ins.name = name
        ins.pc += 2
        h = HANDLERS.get(name)
        try:
            if h is None:
                raise Unsupported(name)
            h(ins, insn)
            ins.do_writebacks()
        except Unsupported as exc:
            ins.out = []
            ins.flow = 'bail'
            ins.why = str(exc)
            self.unsupported.setdefault(name + ': ' + str(exc), []).append(pc)
            ins.length = 2
            return ins
        ins.length = ins.pc - pc
        if ins.flow is None:
            ins.flow = 'fall'
        return ins

    def discover(self, roots):
        work = list(roots)
        self.labels |= set(roots)
        while work:
            pc = work.pop()
            while pc not in self.insns:
                try:
                    ins = self.decode(pc)
                except Unsupported as exc:      # outside the image
                    self.unsupported.setdefault(str(exc), []).append(pc)
                    break
                self.insns[pc] = ins
                nxt = pc + ins.length
                f = ins.flow
                if f in ('call', 'bcc', 'jump') and isinstance(ins.target, int):
                    self.labels.add(ins.target)
                    work.append(ins.target)
                if f in ('call', 'bcc'):
                    self.labels.add(nxt)
                if f in ('fall', 'call', 'bcc'):
                    pc = nxt
                    continue
                break
        # overlapping decodes would make labels land mid-instruction
        starts = sorted(self.insns)
        for a, b in zip(starts, starts[1:]):
            if a + self.insns[a].length > b:
                raise SystemExit('overlapping instructions at %08x / %08x' % (a, b))

    def emit_c(self, out):
        w = out.write
        w('/* Generated by aot/translate.py: the Model:Cycles render, ColdFire -> C. */\n')
        w('#include <stdint.h>\n#include "../runtime.h"\n\n')
        w('#define MC_MAC_MODE(m) (((m) & MACSR_FI) ? 0 : MACSR_SIGNED_INT(m) ? 1 : 2)\n')
        w('MC_INLINE uint32_t mc_mac_word(uint32_t macsr, uint32_t v, int upper)\n{\n'
          '    if (macsr & MACSR_FI) return upper ? (v & 0xffff0000u) : (v << 16);\n'
          '    if (MACSR_SIGNED_INT(macsr)) return upper ? (uint32_t)((int32_t)v >> 16) : (uint32_t)(int16_t)v;\n'
          '    return upper ? (v >> 16) : (uint32_t)(uint16_t)v;\n}\n\n')
        w('#ifdef MC_TRACE\nvoid mc_trace(uint32_t pc, const uint32_t r[16]);\n'
          '#define TRACE(p) do { uint32_t r_[16] = {D0,D1,D2,D3,D4,D5,D6,D7,A0,A1,A2,A3,A4,A5,A6,A7}; '
          'mc_trace(p, r_); } while (0)\n#else\n#define TRACE(p) ((void)0)\n#endif\n\n')
        w('#define LD8(a) mc_ld8m(M, a)\n#define LD16(a) mc_ld16m(M, a)\n#define LD32(a) mc_ld32m(M, a)\n')
        w('#define ST8R(a, v) mc_st8m(M, a, v)\n#define ST16R(a, v) mc_st16m(M, a, v)\n#define ST32R(a, v) mc_st32m(M, a, v)\n')
        for n, mask in ((8, '0xff'), (16, '0xffff'), (32, '0xffffffffu')):
            w('#define ST%d(a, v) do { uint32_t a_ = (a); if (MC_UNLIKELY(a_ >= MC_MMIO_BASE)) '
              'mc_mmio_write(X, a_, %d, (v) & %s); else mc_st%dm(M, a_, v); } while (0)\n' % (n, n // 8, mask, n))
        # MACSR's N/Z/V/EV after a MAC op, computed when read (mc_mac_flags)
        w('#define MAC_LAZY(a) do { mfv = macc[a]; mfp = macpav; mfa = (a); mfl = 1; } while (0)\n')
        w('#define MAC_FLAGS() do { if (mfl) { macflg = mc_mac_flags(macctl | mfp, mfa, mfv); mfl = 0; } } '
          'while (0)\n')
        w('#define SHADOW 64\n')
        w('int mc_run(mc_ctx *X, mc_cpu *cpu, uint32_t entry, uint32_t stop_pc)\n{\n')
        w('    uint8_t *const M = X->mem;\n')
        for i in range(8):
            w('    uint32_t D%d = cpu->d[%d], A%d = cpu->a[%d];\n' % (i, i, i, i))
        w('    uint32_t sr = cpu->sr, ccx = cpu->cc_x, ccn = cpu->cc_n, ccz = cpu->cc_z, '
          'ccv = cpu->cc_v, ccc = cpu->cc_c;\n')
        w('    uint32_t macctl = cpu->macsr & MACSR_CTL, macpav = cpu->macsr & MACSR_PAV, '
          'macflg = cpu->macsr & MACSR_FLAGS, macmask = cpu->mac_mask;\n')
        w('    uint64_t macc[4] = {cpu->macc[0], cpu->macc[1], cpu->macc[2], cpu->macc[3]};\n')
        w('    uint64_t mfv = 0; uint32_t mfp = 0; int mfa = 0, mfl = 0;\n')
        w('    uint32_t t = entry, pc = entry; int status = MC_DONE;\n')
        w('    uint32_t ss_pc[SHADOW]; void *ss_lbl[SHADOW]; int ssp = 0;\n')
        w('    goto dispatch;\n\n')
        starts = sorted(self.insns)
        for k, a in enumerate(starts):      # fall-through across a gap needs a label
            ins = self.insns[a]
            nxt = a + ins.length
            if ins.flow in ('fall', 'bcc', 'call') and not (k + 1 < len(starts) and starts[k + 1] == nxt):
                self.labels.add(nxt)
        for k, a in enumerate(starts):
            ins = self.insns[a]
            if a in self.labels:
                w('L_%08x:\n' % a)
            if a in self.hooks:
                w('    if (X->hook) X->hook(X, 0x%08xu);   /* --hook */\n' % a)
            w('    TRACE(0x%08xu); /* %s */\n' % (a, ins.name))
            nxt = a + ins.length
            follows = k + 1 < len(starts) and starts[k + 1] == nxt
            if ins.flow == 'bail':
                w('    { pc = 0x%08xu; status = MC_FAULT; goto out; } /* %s */\n' % (a, ins.why))
                continue
            body = list(ins.out)
            f = ins.flow
            tgt = ins.target
            if f == 'call':
                # gen_push(s->pc) then jump
                body.append('A7 -= 4; ST32(A7, 0x%08xu);' % nxt)
                body.append('if (ssp < SHADOW) { ss_pc[ssp] = 0x%08xu; ss_lbl[ssp] = &&L_%08x; ssp++; }'
                            % (nxt, nxt))
            w('    {\n')
            for s in body:
                w('        %s\n' % s)
            if f == 'fall':
                if not follows:
                    self.labels.add(nxt)
                    w('        goto L_%08x;\n' % nxt)
            elif f in ('call', 'jump'):
                if isinstance(tgt, int):
                    w('        goto L_%08x;\n' % tgt)
                else:
                    w('        t = %s; goto dispatch;\n' % tgt)
            elif f == 'bcc':
                w('        if (%s) goto L_%08x;\n' % (Insn.cond(ins.cc), tgt))
                if not follows:
                    w('        goto L_%08x;\n' % nxt)
            elif f == 'ret':
                w('        t = LD32(A7); A7 += 4;\n')
                w('        if (t == stop_pc) { pc = t; goto out; }\n')
                w('        while (ssp > 0) { ssp--; if (ss_pc[ssp] == t) goto *ss_lbl[ssp]; }\n')
                w('        goto dispatch;\n')
            elif f == 'rte':
                w('        uint32_t fmt = LD32(A7); t = LD32(A7 + 4);\n')
                w('        A7 = (A7 | ((fmt >> 28) & 3)) + 8;\n')
                w('        if (((fmt ^ sr) & 0x2000u) != 0) { pc = 0x%08xu; status = MC_BAD_SR; goto out; }\n'
                  % a)
                w('        sr = fmt & 0xffe0u; ccc = fmt & 1; ccv = (fmt & 2) ? 0xffffffffu : 0; '
                  'ccz = (fmt & 4) ? 0 : 1; ccn = (fmt & 8) ? 0xffffffffu : 0; ccx = (fmt >> 4) & 1;\n')
                w('        ssp = 0;\n')
                w('        if (t == stop_pc) { pc = t; goto out; }\n')
                w('        goto dispatch;\n')
            w('    }\n')
        # labels whose code could not be decoded: a fault, not a compile error
        for a in sorted(x for x in self.labels if x not in self.insns):
            w('L_%08x: pc = 0x%08xu; status = MC_FAULT; goto out;\n' % (a, a))
        w('\ndispatch:\n    switch (t) {\n')
        for a in sorted(self.labels):
            if a in self.insns:
                w('    case 0x%08xu: goto L_%08x;\n' % (a, a))
        w('    default: pc = t; X->bail_pc = t; status = MC_UNKNOWN_TARGET; goto out;\n    }\n\n')
        w('out:\n')
        for i in range(8):
            w('    cpu->d[%d] = D%d; cpu->a[%d] = A%d;\n' % (i, i, i, i))
        w('    cpu->pc = pc; cpu->sr = sr; cpu->cc_x = ccx; cpu->cc_n = ccn; cpu->cc_z = ccz; '
          'cpu->cc_v = ccv; cpu->cc_c = ccc;\n')
        w('    MAC_FLAGS();\n')
        w('    cpu->macsr = macctl | macpav | macflg; cpu->mac_mask = macmask;\n')
        w('    for (int i = 0; i < 4; i++) cpu->macc[i] = macc[i];\n')
        w('    return status;\n}\n')
        # labels referenced by gotos must exist
        missing = [a for a in self.labels if a not in self.insns]
        return missing


def state_regions(state_dir, lo, hi):
    """-> the (base, bytes) regions of an engine state (dump_state.py's
    regions.bin) that overlap [lo, hi)."""
    out = []
    with open(os.path.join(state_dir, 'regions.bin'), 'rb') as f:
        while True:
            head = f.read(8)
            if len(head) < 8:
                break
            base, n = struct.unpack('<II', head)
            data = f.read(n)
            if base < hi and base + n > lo:
                out.append((base, data))
    return out


def main():
    import argparse
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--sections', default=os.path.join(P, 'data', 'sections'))
    ap.add_argument('--coverage', default=os.path.join(P, 'data', 'coverage.json'))
    ap.add_argument('--out', default=os.path.join(P, 'aot', 'build', 'render.c'))
    ap.add_argument('--state', help='an engine state (dump_state.py) to read code outside the image from')
    ap.add_argument('--code', action='append', default=[], metavar='LO:HI',
                    help='with --state: an address range of code that runs from RAM (hex)')
    ap.add_argument('--hook', action='append', default=[], metavar='PC',
                    help='call the context\'s hook before the instruction at PC (hex); it must start a label')
    args = ap.parse_args()
    img = open(os.path.join(args.sections, 'section_3_MAIN_OS.bin'), 'rb').read()
    table = json.load(open(os.path.join(P, 'aot', 'opcode_table.json')))
    cov = json.load(open(args.coverage))
    # the render's blocks, and the engine calls a host makes between blocks
    # (setparam, setlevel, setmute, trig: NOTES.md "The interface")
    engine = [0x40059534, 0x40059594, 0x400594f0, 0x4005894a]
    roots = sorted(set(int(k, 16) for k in cov['blocks']) | set(engine))
    extra = []
    for rng in args.code:
        lo, hi = (int(x, 16) for x in rng.split(':'))
        extra += state_regions(args.state, lo, hi)
    tr = Translator(img, table, extra)
    tr.hooks = set(int(h, 16) for h in args.hook)
    tr.discover(roots)
    # a hook must be reachable only by falling into it or jumping to its
    # label, both of which pass the call: make each hook PC a label
    tr.labels |= tr.hooks
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    path = args.out
    with open(path, 'w') as f:
        missing = tr.emit_c(f)
    print('%d instructions, %d labels, %d coverage roots -> %s'
          % (len(tr.insns), len(tr.labels), len(roots), path))
    if missing:
        print('labels with no instruction (gotos to code outside the image?):',
              ' '.join('%08x' % a for a in sorted(missing)[:20]))
    for why, pcs in sorted(tr.unsupported.items()):
        print('unsupported %s: %d at %s' % (why, len(pcs), ' '.join('%08x' % p for p in pcs[:6])))


if __name__ == '__main__':
    main()
