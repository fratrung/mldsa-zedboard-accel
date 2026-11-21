#include "backend.h"
#include "xil_io.h"
#include "xparameters.h"

#if MLDSA_BACKEND == 3
#include "xkeccak_multiblock_hw.h"
#include "keccak.h"
#define CTRL XKECCAK_MULTIBLOCK_CONTROL_ADDR_AP_CTRL
#define STATE XKECCAK_MULTIBLOCK_CONTROL_ADDR_STATE_DATA
#else
#include "xkeccak_f1600_hw.h"
#define CTRL XKECCAK_F1600_CONTROL_ADDR_AP_CTRL
#endif

/* Use the v3 BSP address when available. With the v2 BSP the v3 design
 * must reuse its GP0 control segment (0x40000000). */
#ifndef MLDSA_IP_BASE
#if defined(XPAR_KECCAK_MULTIBLOCK_0_BASEADDR) && MLDSA_BACKEND == 3
#define MLDSA_IP_BASE XPAR_KECCAK_MULTIBLOCK_0_BASEADDR
#else
#define MLDSA_IP_BASE XPAR_KECCAK_F1600_0_BASEADDR
#endif
#endif

static void start_and_wait(void)
{
    Xil_Out32(MLDSA_IP_BASE + CTRL, 1);
    while (!(Xil_In32(MLDSA_IP_BASE + CTRL) & 2)) {}
}

#if MLDSA_BACKEND == 1
void hw_init(void) {}

void hw_permute(uint64_t state[25])
{
    uintptr_t addr = MLDSA_IP_BASE + XKECCAK_F1600_CONTROL_ADDR_STATE_BASE;
    for (unsigned i = 0; i < 25; ++i) {
        Xil_Out32(addr + 8*i, (uint32_t)state[i]);
        Xil_Out32(addr + 8*i + 4, (uint32_t)(state[i] >> 32));
    }
    start_and_wait();
    for (unsigned i = 0; i < 25; ++i) {
        uint32_t lo = Xil_In32(addr + 8*i);
        uint32_t hi = Xil_In32(addr + 8*i + 4);
        state[i] = ((uint64_t)hi << 32) | lo;
    }
}
#else
#include <string.h>
#include "xil_cache.h"
#ifdef MLDSA_NTT
#ifdef NTT_BIDIRECTIONAL
#include "xntt_transform_hw.h"
#define NTT_CTRL XNTT_TRANSFORM_CONTROL_ADDR_AP_CTRL
#define NTT_COEFFICIENTS XNTT_TRANSFORM_CONTROL_ADDR_COEFFICIENTS_DATA
#else
#include "xntt_forward_hw.h"
#define NTT_CTRL XNTT_FORWARD_CONTROL_ADDR_AP_CTRL
#define NTT_COEFFICIENTS XNTT_FORWARD_CONTROL_ADDR_COEFFICIENTS_DATA
#endif
#include "xiltimer.h"
/* GP0 segment assigned by hw/vivado/build_multiblock.tcl with argument ntt. */
#define NTT_BASE 0x40010000U
static int32_t ntt_buffer[256] __attribute__((aligned(32)));

static inline void load_ntt(const int32_t coefficients[256])
{
    memcpy(ntt_buffer, coefficients, sizeof ntt_buffer);
    Xil_DCacheFlushRange((INTPTR)ntt_buffer, sizeof ntt_buffer);
}

static inline void run_ntt(int inverse)
{
#ifdef NTT_BIDIRECTIONAL
    Xil_Out32(NTT_BASE + XNTT_TRANSFORM_CONTROL_ADDR_INVERSE_DATA, inverse);
#endif
    Xil_Out32(NTT_BASE + NTT_CTRL, 1);
    while (!(Xil_In32(NTT_BASE + NTT_CTRL) & 2)) {}
}

static inline void read_ntt(int32_t coefficients[256])
{
    Xil_DCacheInvalidateRange((INTPTR)ntt_buffer, sizeof ntt_buffer);
    memcpy(coefficients, ntt_buffer, sizeof ntt_buffer);
}

void hw_ntt(int32_t coefficients[256])
{
    load_ntt(coefficients);
    run_ntt(0);
    read_ntt(coefficients);
}
#ifdef NTT_BIDIRECTIONAL
void hw_invntt(int32_t coefficients[256])
{
    load_ntt(coefficients);
    run_ntt(1);
    read_ntt(coefficients);
}
#endif

/* Independent diagnostic run. Signing and the end-to-end NTT timer use hw_ntt. */
void hw_ntt_profile(int32_t coefficients[256], uint64_t ticks[3], int inverse)
{
    XTime t0, t1, t2, t3;
    XTime_GetTime(&t0);
    load_ntt(coefficients);
    XTime_GetTime(&t1);
    run_ntt(inverse);
    XTime_GetTime(&t2);
    read_ntt(coefficients);
    XTime_GetTime(&t3);
    ticks[0] += t1 - t0;
    ticks[1] += t2 - t1;
    ticks[2] += t3 - t2;
}
#endif
#if MLDSA_BACKEND == 2
#define STATE XKECCAK_F1600_CONTROL_ADDR_STATE_DATA
#endif

/* Entire 32-byte cache lines, including the padding of the last line. */
static uint64_t dma_state[28] __attribute__((aligned(32)));
#if MLDSA_BACKEND == 3
enum { BATCH_BLOCKS = 5 };
static uint64_t dma_out[(BATCH_BLOCKS * KECCAK_SHAKE128_RATE_LANES + 3) / 4 * 4]
    __attribute__((aligned(32)));
#endif

static void set_address(unsigned reg, const void *buffer)
{
    Xil_Out32(MLDSA_IP_BASE + reg, (uint32_t)(uintptr_t)buffer);
    Xil_Out32(MLDSA_IP_BASE + reg + 4, 0); /* Cortex-A9 physical address */
}

void hw_init(void)
{
    set_address(STATE, dma_state);
#ifdef MLDSA_NTT
    Xil_Out32(NTT_BASE + NTT_COEFFICIENTS, (uintptr_t)ntt_buffer);
    Xil_Out32(NTT_BASE + NTT_COEFFICIENTS + 4, 0);
#endif
#if MLDSA_BACKEND == 3
    set_address(XKECCAK_MULTIBLOCK_CONTROL_ADDR_OUT_R_DATA, dma_out);
#endif
}

static void load_state(const uint64_t state[25])
{
    memcpy(dma_state, state, 25 * sizeof *state);
    Xil_DCacheFlushRange((INTPTR)dma_state, sizeof dma_state);
}

static void read_result(uint64_t state[25])
{
    Xil_DCacheInvalidateRange((INTPTR)dma_state, sizeof dma_state);
    memcpy(state, dma_state, 25 * sizeof *state);
}

void hw_permute(uint64_t state[25])
{
    load_state(state);
#if MLDSA_BACKEND == 3
    Xil_Out32(MLDSA_IP_BASE + XKECCAK_MULTIBLOCK_CONTROL_ADDR_COMMAND_DATA, KECCAK_PERMUTE);
#endif
    start_and_wait();
    read_result(state);
}

#if MLDSA_BACKEND == 3
void hw_squeeze(uint8_t *out, size_t blocks, uint64_t state[25], unsigned rate)
{
    if (!blocks) return;
    load_state(state);
    Xil_Out32(MLDSA_IP_BASE + XKECCAK_MULTIBLOCK_CONTROL_ADDR_COMMAND_DATA, KECCAK_SQUEEZE);
    Xil_Out32(MLDSA_IP_BASE + XKECCAK_MULTIBLOCK_CONTROL_ADDR_RATE_LANES_DATA, rate / 8);
    while (blocks) {
        unsigned n = blocks > BATCH_BLOCKS ? BATCH_BLOCKS : (unsigned)blocks;
        Xil_DCacheFlushRange((INTPTR)dma_out, sizeof dma_out);
        Xil_Out32(MLDSA_IP_BASE + XKECCAK_MULTIBLOCK_CONTROL_ADDR_NBLOCKS_DATA, n);
        start_and_wait();
        Xil_DCacheInvalidateRange((INTPTR)dma_out, sizeof dma_out);
        memcpy(out, dma_out, n * rate);
        out += n * rate;
        blocks -= n;
    }
    read_result(state);
}

#ifdef KECCAK_SQUEEZE_PROFILE
#include "xiltimer.h"
/* Separate diagnostic pass over the same operations as one five-block call. */
void hw_squeeze_profile5(uint8_t *out, uint64_t state[25], uint64_t ticks[6])
{
    XTime stamp[7];
    XTime_GetTime(&stamp[0]);
    load_state(state);
    XTime_GetTime(&stamp[1]);
    Xil_Out32(MLDSA_IP_BASE + XKECCAK_MULTIBLOCK_CONTROL_ADDR_COMMAND_DATA, KECCAK_SQUEEZE);
    Xil_Out32(MLDSA_IP_BASE + XKECCAK_MULTIBLOCK_CONTROL_ADDR_RATE_LANES_DATA, KECCAK_SHAKE128_RATE_LANES);
    XTime_GetTime(&stamp[2]);
    Xil_DCacheFlushRange((INTPTR)dma_out, sizeof dma_out);
    XTime_GetTime(&stamp[3]);
    Xil_Out32(MLDSA_IP_BASE + XKECCAK_MULTIBLOCK_CONTROL_ADDR_NBLOCKS_DATA, BATCH_BLOCKS);
    XTime_GetTime(&stamp[4]);
    start_and_wait();
    XTime_GetTime(&stamp[5]);
    Xil_DCacheInvalidateRange((INTPTR)dma_out, sizeof dma_out);
    memcpy(out, dma_out, BATCH_BLOCKS * KECCAK_SHAKE128_RATE_LANES * sizeof *dma_out);
    read_result(state);
    XTime_GetTime(&stamp[6]);
    for (unsigned i = 0; i < 6; ++i) ticks[i] += stamp[i + 1] - stamp[i];
}
#endif
#endif
#endif
