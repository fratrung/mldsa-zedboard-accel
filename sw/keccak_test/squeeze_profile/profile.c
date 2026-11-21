/* Same ELF on each bitstream. XSDB reads the results after profile_done. */
#include <string.h>
#include "backend.h"
#include "fips202.h"
#include "xil_cache.h"
#include "xiltimer.h"

enum { CALLS = 1000, BATCHES = 5, BLOCKS = 5, BYTES = BLOCKS * SHAKE128_RATE };
int benchmark_use_fpga;
void hw_squeeze_profile5(uint8_t *out, uint64_t state[25], uint64_t ticks[6]);

struct result {
    uint32_t passed;
    uint32_t sw_ns;
    uint32_t batch[BATCHES][7];  /* total, then six independently timed phases */
};
struct result profile_result __attribute__((aligned(32)));

static uint32_t mean_ns(uint64_t ticks)
{
    return (uint32_t)(ticks * 1000000000ULL / ((uint64_t)COUNTS_PER_SECOND * CALLS));
}

static uint32_t squeeze_batch(keccak_state *state, uint8_t out[BYTES])
{
    XTime begin, end;
    XTime_GetTime(&begin);
    for (unsigned i = 0; i < CALLS; ++i)
        shake128_squeezeblocks(out, BLOCKS, state);
    XTime_GetTime(&end);
    return mean_ns(end - begin);
}

__attribute__((noinline, noreturn)) void profile_done(void)
{
    for (;;) __asm__ volatile ("nop");
}

int main(void)
{
    static const uint8_t message[] = "ZedBoard ML-DSA benchmark";
    uint8_t expected[BYTES], out[BYTES];
    keccak_state initial, reference, state;
    shake128_absorb_once(&initial, message, sizeof message - 1);
    reference = initial;
    profile_result.sw_ns = squeeze_batch(&reference, expected);
    hw_init();
    benchmark_use_fpga = 1;
    unsigned ok = 1;
    for (unsigned b = 0; b < BATCHES; ++b) {
        state = initial;
        profile_result.batch[b][0] = squeeze_batch(&state, out);
        ok &= !memcmp(out, expected, BYTES) && !memcmp(state.s, reference.s, sizeof state.s);
        state = initial;
        uint64_t ticks[6] = {0};
        for (unsigned i = 0; i < CALLS; ++i)
            hw_squeeze_profile5(out, state.s, ticks);
        ok &= !memcmp(out, expected, BYTES) && !memcmp(state.s, reference.s, sizeof state.s);
        for (unsigned j = 0; j < 6; ++j)
            profile_result.batch[b][j + 1] = mean_ns(ticks[j]);
    }
    profile_result.passed = ok;
    Xil_DCacheFlushRange((INTPTR)&profile_result, sizeof profile_result);
    profile_done();
}
