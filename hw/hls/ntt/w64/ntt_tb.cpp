#include "ntt_forward.h"
#include <cstdio>
#include <cstring>
extern "C" {
#include "../../../../third_party/dilithium/ref/ntt.h"
}
static bool check(const int32_t input[256], unsigned test)
{
    int32_t actual[256], expected[256];
    std::memcpy(actual, input, sizeof actual);
    std::memcpy(expected, input, sizeof expected);
    ntt(expected);
    // Same bytes in DDR: 256 int32 coefficients seen as 128 64-bit words.
    uint64_t words[128];
    std::memcpy(words, actual, sizeof words);
    ntt_forward(words);
    std::memcpy(actual, words, sizeof actual);
    for (unsigned i = 0; i < 256; ++i) {
        if (actual[i] != expected[i]) {
            std::printf("FAIL polynomial %u coefficient %u: %d expected %d\n",
                        test, i, (int)actual[i], (int)expected[i]);
            return false;
        }
    }
    return true;
}

int main()
{
    const int32_t q = 8380417;
    int32_t input[256] = {};
    if (!check(input, 0)) return 1;
    input[255] = 1;
    if (!check(input, 1)) return 1;
    for (unsigned i = 0; i < 256; ++i) input[i] = (i & 1) ? q - 1 : 1 - q;
    if (!check(input, 2)) return 1;
    uint32_t seed = 1;
#ifdef NTT_EXTENDED_TEST
    const unsigned tests = 1000;
#else
    const unsigned tests = 5;
#endif
    for (unsigned t = 0; t < tests; ++t) {
        for (unsigned i = 0; i < 256; ++i) {
            seed = seed * 1664525U + 1013904223U;
            input[i] = (int32_t)(seed % (2U * q - 1)) - (q - 1);
        }
        if (!check(input, 3 + t)) return 1;
    }
    std::printf("PASS %u forward NTT polynomials, 256 coefficients each\n", tests + 3);
    return 0;
}
