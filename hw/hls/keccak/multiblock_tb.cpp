#include <cstdio>
#include <cstring>
#include "keccak.h"

extern "C" void ref_permute(uint64_t state[25]);
extern "C" void ref_squeeze(uint8_t *out, size_t blocks, uint64_t state[25], unsigned rate);

// Match the two m_axi depths used by the RTL co-simulation adapter.
typedef uint64_t State[25];
typedef uint64_t Output[105];

static int compare(const void *got, const void *expected, size_t bytes,
                   const char *name, unsigned lanes, unsigned blocks)
{
    const uint8_t *a = static_cast<const uint8_t *>(got);
    const uint8_t *b = static_cast<const uint8_t *>(expected);
    for (size_t i = 0; i < bytes; ++i) {
        if (a[i] != b[i]) {
            std::printf("FAIL %s lanes=%u blocks=%u byte=%zu got=%02x expected=%02x\n",
                        name, lanes, blocks, i, a[i], b[i]);
            return 1;
        }
    }
    return 0;
}

static int check(const State &initial, unsigned command, unsigned lanes, unsigned blocks)
{
    State state, expected;
    Output out, expected_out;
    std::memcpy(state, initial, sizeof state);
    std::memcpy(expected, initial, sizeof expected);
    std::memset(out, 0xa5, sizeof out);
    std::memset(expected_out, 0xa5, sizeof expected_out);
    if (command == KECCAK_PERMUTE)
        ref_permute(expected);
    else if (command == KECCAK_SQUEEZE && (lanes == 17 || lanes == 21))
        ref_squeeze(reinterpret_cast<uint8_t *>(expected_out), blocks, expected, lanes * 8);

    keccak_multiblock(state, out, blocks, lanes, command);
    // Compare the unused suffix too: all bytes beyond the rate must keep their value.
    return compare(out, expected_out, sizeof out, "output", lanes, blocks)
         + compare(state, expected, sizeof state, "state", lanes, blocks);
}

static int check_continuation(const State &initial, unsigned lanes)
{
    State state, expected;
    Output first = {}, second = {}, expected_out = {};
    std::memcpy(state, initial, sizeof state);
    std::memcpy(expected, initial, sizeof expected);
    ref_squeeze(reinterpret_cast<uint8_t *>(expected_out), 5, expected, lanes * 8);
    keccak_multiblock(state, first, 2, lanes, KECCAK_SQUEEZE);
    keccak_multiblock(state, second, 3, lanes, KECCAK_SQUEEZE);
    return compare(first, expected_out, 2 * lanes * 8, "prefix", lanes, 2)
         + compare(second, expected_out + 2 * lanes, 3 * lanes * 8, "continuation", lanes, 3)
         + compare(state, expected, sizeof state, "continued state", lanes, 5);
}

int main()
{
    State initial;
    for (unsigned i = 0; i < 25; ++i)
        initial[i] = (i + 1) * UINT64_C(0x9e3779b97f4a7c15);
    const unsigned rates[] = {21, 17};
    const unsigned counts[] = {0, 1, 2, 5};
    int errors = 0;
    for (unsigned rate : rates) {
        for (unsigned n : counts)
            errors += check(initial, KECCAK_SQUEEZE, rate, n);
        errors += check_continuation(initial, rate);
    }
    errors += check(initial, KECCAK_PERMUTE, 21, 5);
    errors += check(initial, KECCAK_SQUEEZE, 25, 1);
    errors += check(initial, 99, 17, 1);
    if (!errors)
        std::puts("PASS multiblock: SHAKE128/256, continuation, PERMUTE and invalid commands");
    return errors != 0;
}
