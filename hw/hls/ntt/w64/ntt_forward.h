#ifndef NTT_FORWARD_H
#define NTT_FORWARD_H
#include <stdint.h>

/* ML-DSA forward NTT on one polynomial, in place, reference output order.
 * The 256 coefficients travel as 128 little-endian 64-bit words: word i holds
 * coefficient 2i in bits 0-31 and coefficient 2i+1 in bits 32-63, which is the
 * same DDR layout as int32_t[256]. The 32-bit version is in w32/. */
#define NTT_FORWARD_WORD_BITS 64
void ntt_forward(uint64_t coefficients[128]);
#endif
