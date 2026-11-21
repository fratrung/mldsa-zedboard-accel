#ifndef MLDSA_BACKEND_H
#define MLDSA_BACKEND_H

#include <stddef.h>
#include <stdint.h>

/* One benchmark process owns the accelerator and its DDR buffers. */
extern int benchmark_use_fpga;
#ifdef MLDSA_NTT
extern int benchmark_use_ntt;
void hw_ntt(int32_t coefficients[256]);
#ifdef NTT_BIDIRECTIONAL
extern int benchmark_use_invntt;
void hw_invntt(int32_t coefficients[256]);
#endif
#ifndef HOST_MODEL
void hw_ntt_profile(int32_t coefficients[256], uint64_t ticks[3], int inverse);
#endif
#endif
void hw_init(void);
void hw_permute(uint64_t state[25]);
void hw_squeeze(uint8_t *out, size_t blocks, uint64_t state[25], unsigned rate);

#endif
