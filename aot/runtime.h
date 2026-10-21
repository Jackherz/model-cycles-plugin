/*
 * Runtime for the AOT-compiled Model:Cycles render (aot/translate.py).
 *
 * Guest memory is one 4 GB host reservation: guest address a lives at
 * mem_base + a. ColdFire is big-endian, so loads and stores byte-swap.
 * Peripheral space (>= 0x8C000000, the panel, GPIO and the 0xFC peripheral
 * windows) is ordinary memory too, except that writes there go through
 * mc_mmio_write: it logs them and starts eDMA transfers.
 *
 * The EMAC helpers are ported from QEMU's target/m68k/helper.c with the
 * digikit patches applied (the Unicorn build digiemu runs), so the compiled
 * code computes what the reference emulator computes. QEMU is
 * GPL-2.0-or-later; this file is for personal use with that code in it.
 */
#ifndef MC_RUNTIME_H
#define MC_RUNTIME_H

#include <stdint.h>
#include <string.h>

#if defined(__GNUC__)
#define MC_LIKELY(x) __builtin_expect(!!(x), 1)
#define MC_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define MC_INLINE static inline __attribute__((always_inline))
#else
#define MC_LIKELY(x) (x)
#define MC_UNLIKELY(x) (x)
#define MC_INLINE static inline
#endif

typedef struct {
    uint32_t d[8], a[8];
    uint32_t pc, sr;                 /* sr: system byte and IPL; CCR in cc_* */
    uint32_t cc_x, cc_n, cc_z, cc_v, cc_c;   /* QEMU's flushed representation */
    uint32_t macsr, mac_mask;
    uint64_t macc[4];
} mc_cpu;

/* Status of mc_run. */
enum { MC_DONE = 0, MC_UNKNOWN_TARGET = 1, MC_BAD_SR = 2, MC_FAULT = 3 };

/* One emulated machine: its memory and what the last run did. Instances
 * are independent, so several can run on different threads. */
typedef struct { uint32_t addr, size, value; } mc_mmio_rec;
typedef struct mc_ctx {
    uint8_t *mem;                 /* 4 GB reservation: guest address = mem + addr */
    mc_mmio_rec mmio_log[256];    /* peripheral writes of the last run, for checks */
    uint32_t mmio_count;
    uint32_t bail_pc;             /* target of an MC_UNKNOWN_TARGET */
    uint32_t edma_transfers, edma_refused;
    uint8_t *dma_buf;             /* eDMA staging, allocated with the context */
} mc_ctx;

/* ---- memory --------------------------------------------------------- */

#define MC_MMIO_BASE 0x8C000000u

void mc_mmio_write(mc_ctx *x, uint32_t addr, uint32_t size, uint32_t value);

/* Raw accessors on a memory base (the generated code keeps it in a local). */
MC_INLINE uint32_t mc_ld8m(const uint8_t *m, uint32_t a)  { return m[a]; }
MC_INLINE uint32_t mc_ld16m(const uint8_t *m, uint32_t a)
{
    uint16_t v;
    memcpy(&v, m + a, 2);
    return __builtin_bswap16(v);
}
MC_INLINE uint32_t mc_ld32m(const uint8_t *m, uint32_t a)
{
    uint32_t v;
    memcpy(&v, m + a, 4);
    return __builtin_bswap32(v);
}
MC_INLINE void mc_st8m(uint8_t *m, uint32_t a, uint32_t v)  { m[a] = (uint8_t)v; }
MC_INLINE void mc_st16m(uint8_t *m, uint32_t a, uint32_t v)
{
    uint16_t x = __builtin_bswap16((uint16_t)v);
    memcpy(m + a, &x, 2);
}
MC_INLINE void mc_st32m(uint8_t *m, uint32_t a, uint32_t v)
{
    uint32_t x = __builtin_bswap32(v);
    memcpy(m + a, &x, 4);
}
/* The same on a context, for code outside the render. */
#define mc_ld8(x, a) mc_ld8m((x)->mem, a)
#define mc_ld16(x, a) mc_ld16m((x)->mem, a)
#define mc_ld32(x, a) mc_ld32m((x)->mem, a)
#define mc_st8_raw(x, a, v) mc_st8m((x)->mem, a, v)
#define mc_st16_raw(x, a, v) mc_st16m((x)->mem, a, v)
#define mc_st32_raw(x, a, v) mc_st32m((x)->mem, a, v)

/* ---- EMAC (QEMU target/m68k/helper.c, digikit patches) --------------- */

#define MACSR_PAV0  0x100
#define MACSR_OMC   0x080
#define MACSR_SU    0x040
#define MACSR_FI    0x020
#define MACSR_RT    0x010
#define MACSR_N     0x008
#define MACSR_Z     0x004
#define MACSR_V     0x002
#define MACSR_EV    0x001
#define MACSR_SIGNED_INT(m) (!((m) & (MACSR_FI | MACSR_SU)))

MC_INLINE uint64_t mc_macmuls(uint32_t *macsr, uint32_t op1, uint32_t op2)
{
    int64_t product = (int64_t)(int32_t)op1 * (int32_t)op2;
    int64_t res = (int64_t)((uint64_t)product << 24) >> 24;
    if (res != product) {
        *macsr |= MACSR_V;
        if (*macsr & MACSR_OMC)
            res = product < 0 ? ~(1ll << 50) : (1ll << 50);
    }
    return (uint64_t)res;
}

MC_INLINE uint64_t mc_macmulu(uint32_t *macsr, uint32_t op1, uint32_t op2)
{
    uint64_t product = (uint64_t)op1 * op2;
    if (product & (0xffffffull << 40)) {
        *macsr |= MACSR_V;
        if (*macsr & MACSR_OMC)
            product = 1ll << 50;
        else
            product &= ((1ull << 40) - 1);
    }
    return product;
}

MC_INLINE uint64_t mc_macmulf(uint32_t macsr, uint32_t op1, uint32_t op2)
{
    uint64_t product = (uint64_t)((int64_t)(int32_t)op1 * (int32_t)op2) << 1;
    uint32_t remainder = product & 0xffffff;
    uint64_t field = product >> 24;
    if ((macsr & MACSR_RT) &&
        (remainder > 0x800000 || (remainder == 0x800000 && (field & 1))))
        field++;
    field &= (1ull << 40) - 1;
    if (op1 == 0x80000000u && op2 == 0x80000000u)
        return field;
    return (uint64_t)(((int64_t)(field << 24)) >> 24);
}

MC_INLINE uint64_t mc_macsats(uint32_t *macsr, int acc, uint64_t v)
{
    int64_t tmp = (int64_t)v;
    int64_t result = (int64_t)((uint64_t)tmp << 16) >> 16;
    if (result != tmp) *macsr |= MACSR_V;
    if (*macsr & MACSR_V) {
        *macsr |= MACSR_PAV0 << acc;
        if (*macsr & MACSR_OMC)
            result = tmp < 0 ? (int64_t)~0x7fffffffll : 0x7fffffffll;
    }
    return (uint64_t)result;
}

MC_INLINE uint64_t mc_macsatu(uint32_t *macsr, int acc, uint64_t val)
{
    if (val & (0xffffull << 48)) *macsr |= MACSR_V;
    if (*macsr & MACSR_V) {
        *macsr |= MACSR_PAV0 << acc;
        if (*macsr & MACSR_OMC)
            val = val > (1ull << 53) ? 0 : (1ull << 48) - 1;
        else
            val &= ((1ull << 48) - 1);
    }
    return val;
}

MC_INLINE uint64_t mc_macsatf(uint32_t *macsr, int acc, uint64_t v)
{
    int64_t sum = (int64_t)v;
    int64_t result = (int64_t)((uint64_t)sum << 16) >> 16;
    if (result != sum) *macsr |= MACSR_V;
    if (*macsr & MACSR_V) {
        *macsr |= MACSR_PAV0 << acc;
        if (*macsr & MACSR_OMC)
            result = sum < 0 ? (int64_t)0xffffff8000000000ull : (int64_t)0x007fffffff00ll;
    }
    return (uint64_t)result;
}

MC_INLINE void mc_mac_set_flags(uint32_t *macsr, int acc, uint64_t val)
{
    if (val == 0)
        *macsr |= MACSR_Z;
    else if (val & (1ull << 47))
        *macsr |= MACSR_N;
    if (*macsr & (MACSR_PAV0 << acc))
        *macsr |= MACSR_V;
    if (*macsr & MACSR_FI) {
        int64_t s = ((int64_t)val) >> 39;
        if (s != 0 && s != -1) *macsr |= MACSR_EV;
    } else if (!(*macsr & MACSR_SU)) {
        int64_t s = ((int64_t)val) >> 31;
        if (s != 0 && s != -1) *macsr |= MACSR_EV;
    } else {
        if ((val >> 32) != 0) *macsr |= MACSR_EV;
    }
}

/* The fused single-accumulator MAC/MSAC (digikit helper_mac_fused): clear
 * flags, multiply, scale, accumulate, saturate, set flags. mode 0
 * fractional, 1 signed, 2 unsigned; scale is ext bits 10..9. */
MC_INLINE uint64_t mc_mac(uint32_t *macsr, int acc, uint64_t accv, uint32_t rx, uint32_t ry,
                          int mode, int msac, int scale)
{
    uint64_t product;
    *macsr &= ~(MACSR_V | MACSR_Z | MACSR_N | MACSR_EV);
    if (mode == 0) {
        product = mc_macmulf(*macsr, rx, ry);
    } else {
        product = mode == 1 ? mc_macmuls(macsr, rx, ry) : mc_macmulu(macsr, rx, ry);
        if (scale == 1)
            product <<= 1;
        else if (scale == 3)
            product = mode == 1 ? (uint64_t)((int64_t)product >> 1) : product >> 1;
    }
    accv = msac ? accv - product : accv + product;
    if (mode == 0) accv = mc_macsatf(macsr, acc, accv);
    else if (mode == 1) accv = mc_macsats(macsr, acc, accv);
    else accv = mc_macsatu(macsr, acc, accv);
    mc_mac_set_flags(macsr, acc, accv);
    return accv;
}

MC_INLINE uint32_t mc_get_macf(uint32_t macsr, uint64_t val)
{
    int64_t acc = (int64_t)val;
    int64_t q;
    uint32_t rem;
    if (macsr & MACSR_SU) {
        rem = acc & 0xffffff;
        q = acc >> 24;
        if (rem > 0x800000 || (rem == 0x800000 && (q & 1))) q++;
        if ((macsr & MACSR_OMC) && q != (int16_t)q) q = acc < 0 ? -0x8000 : 0x7fff;
        return (uint32_t)q & 0xffff;
    }
    rem = acc & 0xff;
    q = acc >> 8;
    if ((macsr & MACSR_RT) && (rem > 0x80 || (rem == 0x80 && (q & 1)))) q++;
    if ((macsr & MACSR_OMC) && q != (int32_t)q) q = acc < 0 ? INT32_MIN : INT32_MAX;
    return (uint32_t)q;
}

MC_INLINE uint32_t mc_get_macs(uint64_t val)
{
    if (val == (uint64_t)(int64_t)(int32_t)val) return (uint32_t)(int32_t)val;
    return (int64_t)val < 0 ? 0x80000000u : 0x7fffffffu;
}

MC_INLINE uint32_t mc_get_macu(uint64_t val)
{
    return (val >> 32) == 0 ? (uint32_t)val : 0xffffffffu;
}

/* move.l ACCn,Rx (from_mac): the value by mode. */
MC_INLINE uint32_t mc_from_mac(uint32_t macsr, uint64_t acc)
{
    if (macsr & MACSR_FI) return mc_get_macf(macsr, acc);
    if (!(macsr & MACSR_OMC)) return (uint32_t)acc;
    if (MACSR_SIGNED_INT(macsr)) return mc_get_macs(acc);
    return mc_get_macu(acc);
}

/* move.l <ea>,ACCn (to_mac) */
MC_INLINE uint64_t mc_to_mac(uint32_t macsr, uint32_t val)
{
    if (macsr & MACSR_FI) return (uint64_t)((int64_t)(int32_t)val) << 8;
    if (MACSR_SIGNED_INT(macsr)) return (uint64_t)(int64_t)(int32_t)val;
    return (uint64_t)val;
}

/* move.l <ea>,MACSR: repack the accumulators when FI or SU change. */
MC_INLINE void mc_set_macsr(uint32_t *macsr, uint64_t macc[4], uint32_t val)
{
    if ((*macsr ^ val) & (MACSR_FI | MACSR_SU)) {
        for (int i = 0; i < 4; i++) {
            uint64_t regval = macc[i];
            int8_t exthigh = (int8_t)(regval >> 40);
            uint32_t acc;
            uint8_t extlow;
            if (*macsr & MACSR_FI) {
                acc = (uint32_t)(regval >> 8);
                extlow = (uint8_t)regval;
            } else {
                acc = (uint32_t)regval;
                extlow = (uint8_t)(regval >> 32);
            }
            if (val & MACSR_FI) {
                regval = (((uint64_t)acc) << 8) | extlow;
                regval |= (uint64_t)((int64_t)exthigh) << 40;
            } else if (!(val & MACSR_SU)) {
                regval = acc | (uint64_t)(((int64_t)extlow) << 32);
                regval |= (uint64_t)((int64_t)exthigh) << 40;
            } else {
                regval = acc | (((uint64_t)extlow) << 32);
                regval |= ((uint64_t)(uint8_t)exthigh) << 40;
            }
            macc[i] = regval;
        }
    }
    *macsr = val;
}

MC_INLINE uint32_t mc_get_mac_extf(const uint64_t macc[4], int acc)
{
    uint32_t val = macc[acc] & 0x00ff;
    val |= (macc[acc] >> 32) & 0xff00;
    val |= (macc[acc + 1] << 16) & 0x00ff0000;
    val |= (macc[acc + 1] >> 16) & 0xff000000;
    return val;
}

MC_INLINE uint32_t mc_get_mac_exti(const uint64_t macc[4], int acc)
{
    uint32_t val = (macc[acc] >> 32) & 0xffff;
    val |= (macc[acc + 1] >> 16) & 0xffff0000;
    return val;
}

MC_INLINE void mc_set_mac_extf(uint64_t macc[4], uint32_t val, int acc)
{
    int64_t res;
    int32_t tmp;
    res = macc[acc] & 0xffffffff00ull;
    tmp = (int16_t)(val & 0xff00);
    res |= ((int64_t)tmp) << 32;
    res |= val & 0xff;
    macc[acc] = res;
    res = macc[acc + 1] & 0xffffffff00ull;
    tmp = (int32_t)(val & 0xff000000);
    res |= ((int64_t)tmp) << 16;
    res |= (val >> 16) & 0xff;
    macc[acc + 1] = res;
}

MC_INLINE void mc_set_mac_exts(uint64_t macc[4], uint32_t val, int acc)
{
    int64_t res;
    int32_t tmp;
    res = (uint32_t)macc[acc];
    tmp = (int16_t)val;
    res |= ((int64_t)tmp) << 32;
    macc[acc] = res;
    res = (uint32_t)macc[acc + 1];
    tmp = (int32_t)(val & 0xffff0000);
    res |= (int64_t)tmp << 16;
    macc[acc + 1] = res;
}

MC_INLINE void mc_set_mac_extu(uint64_t macc[4], uint32_t val, int acc)
{
    uint64_t res;
    res = (uint32_t)macc[acc];
    res |= ((uint64_t)(val & 0xffff)) << 32;
    macc[acc] = res;
    res = (uint32_t)macc[acc + 1];
    res |= (uint64_t)(val & 0xffff0000) << 16;
    macc[acc + 1] = res;
}

/* ---- integer helpers (QEMU op_helper.c) ------------------------------ */

MC_INLINE uint32_t mc_sats(uint32_t val, uint32_t v)
{
    if ((int32_t)v < 0) val = (uint32_t)(((int32_t)val) >> 31) ^ 0x80000000u;
    return val;
}

MC_INLINE uint32_t mc_ff1(uint32_t x)
{
    int n;
    for (n = 32; x; n--) x >>= 1;
    return (uint32_t)n;
}

/* ---- the API the harness and the plugin use -------------------------- */

mc_ctx *mc_ctx_new(void);                    /* reserves the 4 GB guest space */
void mc_ctx_free(mc_ctx *x);
void mc_mem_write(mc_ctx *x, uint32_t addr, const void *src, uint32_t n);
void mc_mem_read(mc_ctx *x, uint32_t addr, void *dst, uint32_t n);
uint32_t mc_ctx_mmio_count(const mc_ctx *x);
const mc_mmio_rec *mc_ctx_mmio_log(const mc_ctx *x);
void mc_ctx_mmio_clear(mc_ctx *x);

int mc_run(mc_ctx *x, mc_cpu *cpu, uint32_t entry, uint32_t stop_pc);

#endif
