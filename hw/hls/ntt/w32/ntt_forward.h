#ifndef NTT_FORWARD_H
#define NTT_FORWARD_H
#include <stdint.h>

/* ML-DSA forward NTT on one polynomial, in place, reference output order. */
void ntt_forward(int32_t coefficients[256]);
#endif
