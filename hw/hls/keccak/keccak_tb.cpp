/*
 * Testbench for keccak_f1600(). It runs only on the PC (C simulation and
 * C/RTL co-simulation) and never becomes hardware.
 *
 * It compares keccak_f1600() with the original permutation of the reference
 * code (ref_permute(), in tb_ref.c) on the all-zero state and on N_RANDOM
 * pseudo-random states. Returns 0 if all results are equal, 1 otherwise.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "keccak.h"

extern "C" void ref_permute(uint64_t state[25]);

#define N_RANDOM 1000

/* splitmix64: a simple 64-bit generator; the same seed gives the same states. */
static uint64_t next_random(uint64_t *seed)
{
    uint64_t z = (*seed += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

/* Permutes two copies of in[], one per function, and compares them. */
static int check(const uint64_t in[25], int test)
{
    uint64_t expected[25], got[25];

    memcpy(expected, in, sizeof expected);
    memcpy(got, in, sizeof got);
    ref_permute(expected);
    keccak_f1600(got);

    for (int i = 0; i < 25; i++) {
        if (got[i] != expected[i]) {
            printf("FAIL: state %d, lane %d (x=%d, y=%d): got %016llx, expected %016llx\n",
                   test, i, i % 5, i / 5,
                   (unsigned long long)got[i], (unsigned long long)expected[i]);
            return 1;
        }
    }
    return 0;
}

int main()
{
    uint64_t state[25];
    uint64_t seed = 1;
    int errors = 0;

    /* State 0: all zeros. */
    memset(state, 0, sizeof state);
    errors += check(state, 0);

    /* States 1..N_RANDOM: pseudo-random. */
    for (int t = 1; t <= N_RANDOM; t++) {
        for (int i = 0; i < 25; i++)
            state[i] = next_random(&seed);
        errors += check(state, t);
    }

    if (errors == 0)
        printf("PASS: %d states, all equal to the original\n", N_RANDOM + 1);
    else
        printf("FAIL: %d of %d states differ from the original\n", errors, N_RANDOM + 1);

    return errors ? 1 : 0;
}
