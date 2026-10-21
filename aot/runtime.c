/*
 * Runtime for the AOT render: guest memory, peripheral writes, eDMA.
 *
 * The eDMA model is digiemu's emu/edma_sw.py (software-started channels run
 * to completion when the guest sets START), ported to C: same TCD layout,
 * modulo rings, scatter-gather chains and completion value.
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "runtime.h"

#define MAX_BYTES (1u << 20)
#define MEM_SIZE (((size_t)1 << 32) + 0x1000)   /* + slack: a 4-byte access at 0xFFFFFFFF */

mc_ctx *mc_ctx_new(void)
{
    mc_ctx *x = calloc(1, sizeof *x);
    if (!x)
        return NULL;
    void *p = mmap(NULL, MEM_SIZE, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
    if (p == MAP_FAILED) {
        free(x);
        return NULL;
    }
    x->mem = p;
    /* allocated and touched here, never on the audio thread */
    x->dma_buf = malloc(MAX_BYTES);
    if (!x->dma_buf) {
        munmap(p, MEM_SIZE);
        free(x);
        return NULL;
    }
    memset(x->dma_buf, 0, MAX_BYTES);   /* calloc would leave the pages untouched */
    return x;
}

void mc_ctx_free(mc_ctx *x)
{
    if (!x)
        return;
    munmap(x->mem, MEM_SIZE);
    free(x->dma_buf);
    free(x);
}

void mc_mem_write(mc_ctx *x, uint32_t addr, const void *src, uint32_t n) { memcpy(x->mem + addr, src, n); }
void mc_mem_read(mc_ctx *x, uint32_t addr, void *dst, uint32_t n) { memcpy(dst, x->mem + addr, n); }
uint32_t mc_ctx_mmio_count(const mc_ctx *x) { return x->mmio_count; }
const mc_mmio_rec *mc_ctx_mmio_log(const mc_ctx *x) { return x->mmio_log; }
void mc_ctx_mmio_clear(mc_ctx *x) { x->mmio_count = 0; }

/* ---- eDMA (digiemu edma_sw.py) --------------------------------------- */

#define TCD_BASE 0xFC045000u
#define TCD_END  (TCD_BASE + 64 * 0x20)
#define CSR_OFF 0x1E
#define CSR_START 0x0001u
#define CSR_ESG 0x0010u
#define CSR_ACTIVE 0x0040u
#define CSR_DONE 0x0080u
#define MAX_LINKS 64


static int claimed(uint32_t ch)
{
    return ch == 35 || ch == 48 || ch == 50 || ch == 52 || ch == 54 || ch == 59;
}

static uint32_t modulo_add(uint32_t addr, int32_t delta, uint32_t mod)
{
    if (!mod)
        return addr + (uint32_t)delta;
    uint32_t mask = (1u << mod) - 1;
    return (addr & ~mask) | ((addr + (uint32_t)delta) & mask);
}

typedef struct {
    uint32_t saddr; uint16_t attr; int16_t soff; uint32_t nbytes, slast, daddr;
    uint16_t citer; int16_t doff; uint32_t dlast; uint16_t biter, csr;
} tcd_t;

static void tcd_read(mc_ctx *x, uint32_t at, tcd_t *t)
{
    t->saddr = mc_ld32(x, at); t->attr = mc_ld16(x, at + 4); t->soff = (int16_t)mc_ld16(x, at + 6);
    t->nbytes = mc_ld32(x, at + 8); t->slast = mc_ld32(x, at + 0xC); t->daddr = mc_ld32(x, at + 0x10);
    t->citer = mc_ld16(x, at + 0x14); t->doff = (int16_t)mc_ld16(x, at + 0x16);
    t->dlast = mc_ld32(x, at + 0x18); t->biter = mc_ld16(x, at + 0x1C); t->csr = mc_ld16(x, at + 0x1E);
}

static void tcd_write(mc_ctx *x, uint32_t at, const tcd_t *t)
{
    mc_st32_raw(x, at, t->saddr); mc_st16_raw(x, at + 4, t->attr); mc_st16_raw(x, at + 6, (uint16_t)t->soff);
    mc_st32_raw(x, at + 8, t->nbytes); mc_st32_raw(x, at + 0xC, t->slast); mc_st32_raw(x, at + 0x10, t->daddr);
    mc_st16_raw(x, at + 0x14, t->citer); mc_st16_raw(x, at + 0x16, (uint16_t)t->doff);
    mc_st32_raw(x, at + 0x18, t->dlast); mc_st16_raw(x, at + 0x1C, t->biter); mc_st16_raw(x, at + 0x1E, t->csr);
}

static uint32_t iteration_count(uint16_t raw) { return raw & 0x8000 ? raw & 0x1FF : raw & 0x7FFF; }


/* One major loop; -1 on a descriptor digiemu refuses. */
static int run_tcd(mc_ctx *x, uint32_t tcd_at, tcd_t *t)
{
    uint8_t *dma_buf = x->dma_buf;
    uint8_t *mc_mem = x->mem;
    uint32_t citer = iteration_count(t->citer);
    if (!citer || !t->nbytes || (uint64_t)citer * t->nbytes > MAX_BYTES)
        return -1;
    uint32_t ssize = 1u << ((t->attr >> 8) & 7), dsize = 1u << (t->attr & 7);
    uint32_t smod = t->attr >> 11, dmod = (t->attr >> 3) & 0x1F;
    if (t->nbytes % ssize || t->nbytes % dsize)
        return -1;
    uint32_t total = citer * t->nbytes, source, dest;
    if (t->soff == (int32_t)ssize && t->doff == (int32_t)dsize) {
        /* whole major loop as one block copy: read all, then write all */
        uint32_t a = t->saddr;
        for (uint32_t i = 0; i < total; i++) { dma_buf[i] = mc_mem[a]; a = modulo_add(a, 1, smod); }
        source = a;
        a = t->daddr;
        for (uint32_t i = 0; i < total; i++) { mc_mem[a] = dma_buf[i]; a = modulo_add(a, 1, dmod); }
        dest = a;
    } else {
        source = t->saddr; dest = t->daddr;
        for (uint32_t it = 0; it < citer; it++) {
            uint32_t k = 0;
            for (uint32_t b = 0; b < t->nbytes / ssize; b++) {
                memcpy(dma_buf + k, mc_mem + source, ssize); k += ssize;
                source = modulo_add(source, t->soff, smod);
            }
            k = 0;
            for (uint32_t b = 0; b < t->nbytes / dsize; b++) {
                memcpy(mc_mem + dest, dma_buf + k, dsize); k += dsize;
                dest = modulo_add(dest, t->doff, dmod);
            }
        }
    }
    t->saddr = source + t->slast;
    t->daddr = (t->csr & CSR_ESG) ? dest : dest + t->dlast;
    t->citer = t->biter;
    tcd_write(x, tcd_at, t);
    return 0;
}

static void edma_start(mc_ctx *x, uint32_t ch)
{
    uint8_t *mc_mem = x->mem;
    uint32_t at = TCD_BASE + ch * 0x20;
    tcd_t t;
    tcd_read(x, at, &t);
    uint32_t seen[MAX_LINKS];
    int nseen = 0;
    for (int link = 0;; link++) {
        if (link == MAX_LINKS || run_tcd(x, at, &t) < 0) { x->edma_refused++; return; }
        if (!(t.csr & CSR_ESG))
            break;
        uint32_t nxt = t.dlast;
        for (int i = 0; i < nseen; i++)
            if (seen[i] == nxt) { x->edma_refused++; return; }
        if (nxt % 0x20) { x->edma_refused++; return; }
        seen[nseen++] = nxt;
        memcpy(mc_mem + at, mc_mem + nxt, 0x20);   /* hardware loads the next descriptor */
        tcd_read(x, at, &t);
    }
    x->edma_transfers++;
    mc_st16_raw(x, at + CSR_OFF, (t.csr & ~(CSR_START | CSR_ACTIVE)) | CSR_DONE);
}

void mc_mmio_write(mc_ctx *x, uint32_t addr, uint32_t size, uint32_t value)
{
    if (x->mmio_count < 256)
        x->mmio_log[x->mmio_count] = (mc_mmio_rec){addr, size, value};
    x->mmio_count++;
    if (size == 1) mc_st8_raw(x, addr, value);
    else if (size == 2) mc_st16_raw(x, addr, value);
    else mc_st32_raw(x, addr, value);
    if (addr >= TCD_BASE && addr < TCD_END) {
        uint32_t ch = (addr - TCD_BASE) / 0x20, off = (addr - TCD_BASE) % 0x20;
        if (off + size > CSR_OFF && off <= CSR_OFF + 1 && !claimed(ch)) {
            if (mc_ld16(x, TCD_BASE + ch * 0x20 + CSR_OFF) & CSR_START)
                edma_start(x, ch);
        }
    }
}

/* ---- per-instruction trace (built with -DMC_TRACE) ------------------- */
#ifdef MC_TRACE
uint32_t *mc_trace_buf;
uint32_t mc_trace_cap, mc_trace_len;
void mc_trace(uint32_t pc, const uint32_t r[16])
{
    if (mc_trace_len < mc_trace_cap) {
        uint32_t *e = mc_trace_buf + 17 * (size_t)mc_trace_len;
        e[0] = pc;
        memcpy(e + 1, r, 64);
    }
    mc_trace_len++;
}
#endif
