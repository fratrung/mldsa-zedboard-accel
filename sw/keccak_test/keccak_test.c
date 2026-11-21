/*
 * keccak_test.c - first test of the Keccak-f[1600] accelerator on the ZedBoard.
 *
 * Runs on the ARM (Cortex-A9 #0, bare metal). Version 2, for the m_axi
 * accelerator (version 1, AXI-Lite only, is in v1_axilite/). For each test state:
 *   1. the state is copied into a buffer in DDR and the data cache is flushed,
 *      so that the DDR really contains it (the accelerator does not see the cache);
 *   2. "start" is written to the accelerator, which reads the state from DDR
 *      with one burst, permutes it and writes it back with one burst;
 *   3. after "done" the cache lines of the buffer are invalidated, so that the
 *      ARM reads the new values from DDR, and the state is copied back;
 * then the result is compared with the original permutation of the reference code
 * (KeccakF1600_StatePermute in third_party/dilithium/ref/fips202.c).
 *
 * The states are the same as in the HLS testbench (hw/hls/keccak/keccak_tb.cpp):
 * the all-zero state, then N_RANDOM states from splitmix64 with seed 1.
 *
 * Then it measures the average time of one permutation in hardware
 * (transfers included) and in software, with the ARM global timer.
 *
 * Output on the USB-UART (J14), 115200 8N1. The results are printed again
 * every 2 seconds, forever: the PC has only one free USB port, so the same
 * cable is moved from PROG (J17) to UART (J14) after the program has started.
 */

#include <stdint.h>
#include <string.h>

#include "xil_io.h"
#include "xil_cache.h"
#include "xil_printf.h"
#include "xiltimer.h"
#include "sleep.h"
#include "xparameters.h"
#include "xkeccak_f1600_hw.h"

/*
 * The original permutation is "static" in fips202.c: including the file
 * makes it visible here without modifying third_party/ (as in tb_ref.c).
 */
#include "fips202.c"

#define N_RANDOM 1000
#define N_TIMING 10000

/* Registers of the accelerator (addresses from xparameters.h and the HLS driver). */
#define KECCAK_BASE  XPAR_KECCAK_F1600_0_BASEADDR
#define KECCAK_CTRL  (KECCAK_BASE + XKECCAK_F1600_CONTROL_ADDR_AP_CTRL)
#define KECCAK_ADDR  (KECCAK_BASE + XKECCAK_F1600_CONTROL_ADDR_STATE_DATA)
#define AP_START 0x1
#define AP_DONE  0x2

/*
 * Buffer in DDR read and written by the accelerator. 28 lanes = 224 bytes =
 * 7 cache lines of 32 bytes, aligned: flushing/invalidating it never touches
 * other variables that share a cache line.
 */
static uint64_t dma_state[28] __attribute__((aligned(32)));

/* Tells the accelerator where the buffer is (64-bit address register, low word first). */
static void hw_init(void)
{
    Xil_Out32(KECCAK_ADDR,     (u32)(UINTPTR)dma_state);
    Xil_Out32(KECCAK_ADDR + 4, 0);
}

/* Permutes state[] with the accelerator. */
static void hw_permute(uint64_t state[25])
{
    memcpy(dma_state, state, 25 * sizeof(uint64_t));
    Xil_DCacheFlushRange((INTPTR)dma_state, sizeof dma_state);

    Xil_Out32(KECCAK_CTRL, AP_START);
    while ((Xil_In32(KECCAK_CTRL) & AP_DONE) == 0)
        ;

    Xil_DCacheInvalidateRange((INTPTR)dma_state, sizeof dma_state);
    memcpy(state, dma_state, 25 * sizeof(uint64_t));
}

/* splitmix64, as in the HLS testbench: the same seed gives the same states. */
static uint64_t next_random(uint64_t *seed)
{
    uint64_t z = (*seed += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

static void print_lane(uint64_t x)
{
    xil_printf("%08x%08x", (u32)(x >> 32), (u32)x);
}

/* First mismatch found (if any), printed in the summary. */
static int fail_test = -1, fail_lane = -1;
static uint64_t fail_got, fail_expected;

/* Permutes two copies of in[], one in hardware and one in software, and compares them. */
static int check(const uint64_t in[25], int test)
{
    uint64_t expected[25], got[25];

    memcpy(expected, in, sizeof expected);
    memcpy(got, in, sizeof got);
    KeccakF1600_StatePermute(expected);
    hw_permute(got);

    for (int i = 0; i < 25; i++) {
        if (got[i] != expected[i]) {
            if (fail_test < 0) {
                fail_test = test;
                fail_lane = i;
                fail_got = got[i];
                fail_expected = expected[i];
            }
            return 1;
        }
    }
    return 0;
}

/*
 * Global timer ticks -> nanoseconds. COUNTS_PER_SECOND is defined in the BSP
 * as "XPAR_CPU_CORE_CLOCK_FREQ_HZ/2" without parentheses: keep them here.
 */
static u32 ticks_to_ns(XTime ticks)
{
    return (u32)((ticks * 1000000000ULL) / (COUNTS_PER_SECOND));
}

int main(void)
{
    uint64_t state[25];
    uint64_t seed = 1;
    int errors = 0;
    XTime t0, t1;
    u32 hw_ns, sw_ns;

    hw_init();

    /* Correctness: state 0 (all zeros), then N_RANDOM pseudo-random states. */
    memset(state, 0, sizeof state);
    errors += check(state, 0);
    for (int t = 1; t <= N_RANDOM; t++) {
        for (int i = 0; i < 25; i++)
            state[i] = next_random(&seed);
        errors += check(state, t);
    }

    /* Timing: average over N_TIMING permutations of the same state. */
    XTime_GetTime(&t0);
    for (int k = 0; k < N_TIMING; k++)
        hw_permute(state);
    XTime_GetTime(&t1);
    hw_ns = ticks_to_ns((t1 - t0) / N_TIMING);

    XTime_GetTime(&t0);
    for (int k = 0; k < N_TIMING; k++)
        KeccakF1600_StatePermute(state);
    XTime_GetTime(&t1);
    sw_ns = ticks_to_ns((t1 - t0) / N_TIMING);

    /* Summary, repeated every 2 seconds. */
    for (int n = 1; ; n++) {
        xil_printf("\r\n=== Keccak-f[1600] accelerator test, m_axi version (print %d) ===\r\n", n);
        xil_printf("accelerator at 0x%08x, state buffer in DDR at 0x%08x\r\n", (u32)KECCAK_BASE, (u32)(UINTPTR)dma_state);
        if (errors == 0) {
            xil_printf("PASS: %d states, all equal to the original\r\n", N_RANDOM + 1);
        } else {
            xil_printf("FAIL: %d of %d states differ from the original\r\n", errors, N_RANDOM + 1);
            xil_printf("first difference: state %d, lane %d: got ", fail_test, fail_lane);
            print_lane(fail_got);
            xil_printf(", expected ");
            print_lane(fail_expected);
            xil_printf("\r\n");
        }
        xil_printf("hardware (transfers included): %d ns per permutation\r\n", (int)hw_ns);
        xil_printf("software (reference code):     %d ns per permutation\r\n", (int)sw_ns);
        /* A result lane, so that the compiler cannot drop the software loop. */
        xil_printf("(lane 0 after the timing loops: ");
        print_lane(state[0]);
        xil_printf(")\r\n");
        sleep(2);
    }
    return 0;
}
