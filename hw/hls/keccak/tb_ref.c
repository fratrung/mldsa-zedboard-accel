/*
 * Testbench only: gives access to the original Keccak-f[1600] permutation.
 *
 * KeccakF1600_StatePermute() is static in fips202.c, so it cannot be called
 * from another file. Including fips202.c here puts it in this file, without
 * modifying third_party/.
 */

#include <stdint.h>
#include "../../../third_party/dilithium/ref/fips202.c"

#ifdef __cplusplus
extern "C"
#endif
void ref_permute(uint64_t state[25])
{
  KeccakF1600_StatePermute(state);
}

#ifdef __cplusplus
extern "C"
#endif
void ref_squeeze(uint8_t *out, size_t blocks, uint64_t state[25], unsigned rate)
{
  keccak_squeezeblocks(out, blocks, state, rate);
}
