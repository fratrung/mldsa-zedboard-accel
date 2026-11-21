/* Native functional test: call the HLS C++ top selected for this bitstream. */
#include "keccak.h"
#ifdef MLDSA_NTT
#include "ntt_forward.h"
#endif
#include <cstring>
extern "C" {
#include "backend.h"

void hw_init(void) {}
#ifdef MLDSA_NTT
static void transform(int32_t coefficients[256], bool inverse)
{
#if defined(NTT_FORWARD_WORD_BITS) && NTT_FORWARD_WORD_BITS == 64
    uint64_t words[128]; /* same bytes as the DDR buffer on the board */
    std::memcpy(words, coefficients, sizeof words);
#ifdef NTT_BIDIRECTIONAL
    ntt_transform(words, inverse);
#else
    ntt_forward(words);
#endif
    std::memcpy(coefficients, words, sizeof words);
#else
    ntt_forward(coefficients);
#endif
}
void hw_ntt(int32_t coefficients[256]) { transform(coefficients, false); }
#ifdef NTT_BIDIRECTIONAL
void hw_invntt(int32_t coefficients[256]) { transform(coefficients, true); }
#endif
#endif
void hw_permute(uint64_t state[25])
{
#if MLDSA_BACKEND == 3
    keccak_multiblock(state, nullptr, 0, 0, KECCAK_PERMUTE);
#else
    keccak_f1600(state);
#endif
}

#if MLDSA_BACKEND == 3
void hw_squeeze(uint8_t *out, size_t blocks, uint64_t state[25], unsigned rate)
{
    uint64_t lanes[5 * KECCAK_SHAKE128_RATE_LANES];
    while (blocks) {
        unsigned n = blocks > 5 ? 5 : (unsigned)blocks;
        keccak_multiblock(state, lanes, n, rate / 8, KECCAK_SQUEEZE);
        std::memcpy(out, lanes, n * rate);
        blocks -= n;
        out += n * rate;
    }
}
#endif
}
