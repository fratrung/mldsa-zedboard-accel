#ifndef KECCAK_H
#define KECCAK_H

#include <stdint.h>

/* Keccak-f[1600] permutation of the 25-lane state, in place (HLS top). */
void keccak_f1600(uint64_t state[25]);

/* Multiblock IP interface. The v2 top above remains the timing baseline. */
enum {
    KECCAK_PERMUTE = 0,
    KECCAK_SQUEEZE = 1,
    KECCAK_SHAKE128_RATE_LANES = 21,
    KECCAK_SHAKE256_RATE_LANES = 17
};

/* PERMUTE updates state once. SQUEEZE emits nblocks * rate_lanes words
 * into a separate DDR buffer and updates state after the last block. */
void keccak_multiblock(uint64_t state[25], uint64_t *out, uint32_t nblocks,
                       uint32_t rate_lanes, uint32_t command);

#endif
