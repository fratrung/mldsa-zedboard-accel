/* Reproducible ML-DSA benchmark. All key and signing inputs are public test data. */
#include <string.h>
#include "backend.h"
#include "fips202.h"
#include "randombytes.h"
#include "sign.h"

#ifdef HOST_MODEL
#include <stdio.h>
#define report printf
#else
#include "xil_printf.h"
#include "xiltimer.h"
#include "sleep.h"
#define report xil_printf
#endif

/* Signing restarts a random number of times, so a single message is not a fair
 * sample. Message i is the fixed text followed by the 4 little-endian bytes of i. */
#ifndef MESSAGES
#define MESSAGES 1000
#endif
#define TEXT "ZedBoard ML-DSA benchmark"
enum { TEXT_BYTES = sizeof TEXT - 1 };

static uint8_t pk[CRYPTO_PUBLICKEYBYTES], sk[CRYPTO_SECRETKEYBYTES];
static uint8_t reference_signatures[MESSAGES][CRYPTO_BYTES], signatures[MESSAGES][CRYPTO_BYTES];
static uint8_t message[TEXT_BYTES + 4] = TEXT;
static const uint8_t context[] = "Embedded Systems";
static unsigned ref_sign_us, ref_verify_us, sign_us, verify_us;
int benchmark_use_fpga;
#ifdef MLDSA_NTT
#include "ntt.h"
int benchmark_use_ntt;
static unsigned keccak_sign_us, keccak_verify_us;
#ifdef NTT_BIDIRECTIONAL
int benchmark_use_invntt;
static unsigned forward_sign_us, forward_verify_us;
#endif
#endif

#ifndef HOST_MODEL
enum { SQUEEZE_BLOCKS = 5, SQUEEZE_REPEATS = 1000 };
static unsigned squeeze_ref_ns, squeeze_target_ns;
#ifdef MLDSA_NTT
enum { NTT_SAMPLES = 128 };
static int32_t ntt_reference[NTT_SAMPLES][N], ntt_selected[NTT_SAMPLES][N];
struct ntt_measurement {
    unsigned ref_ns, target_ns, phase_ns[3];
};
static struct ntt_measurement forward;
#ifdef NTT_BIDIRECTIONAL
static struct ntt_measurement inverse;
#endif

static unsigned time_ntt(int32_t polys[NTT_SAMPLES][N], int inv)
{
    XTime begin, end;
    XTime_GetTime(&begin);
    for (unsigned i = 0; i < NTT_SAMPLES; ++i) {
        if (inv) invntt_tomont(polys[i]);
        else ntt(polys[i]);
    }
    XTime_GetTime(&end);
    return (unsigned)((end - begin) * 1000000000ULL /
                      ((uint64_t)COUNTS_PER_SECOND * NTT_SAMPLES));
}

static void fill_ntt_inputs(int32_t polys[NTT_SAMPLES][N])
{
    uint32_t seed = 1;
    for (unsigned i = 0; i < NTT_SAMPLES; ++i)
        for (unsigned j = 0; j < N; ++j) {
            seed = seed * 1664525U + 1013904223U;
            polys[i][j] = (int32_t)(seed % (2U * Q - 1)) - (Q - 1);
        }
}

static int check_ntt(int inv, struct ntt_measurement *result)
{
    fill_ntt_inputs(ntt_reference);
    memcpy(ntt_selected, ntt_reference, sizeof ntt_selected);
    benchmark_use_ntt = 0;
#ifdef NTT_BIDIRECTIONAL
    benchmark_use_invntt = 0;
#endif
    result->ref_ns = time_ntt(ntt_reference, inv);
    benchmark_use_ntt = 1;
#ifdef NTT_BIDIRECTIONAL
    benchmark_use_invntt = inv;
#endif
    result->target_ns = time_ntt(ntt_selected, inv);
    if (memcmp(ntt_selected, ntt_reference, sizeof ntt_selected)) return 1;
    uint64_t ticks[3] = {0};
    fill_ntt_inputs(ntt_selected);
    for (unsigned i = 0; i < NTT_SAMPLES; ++i)
        hw_ntt_profile(ntt_selected[i], ticks, inv);
    for (unsigned i = 0; i < 3; ++i)
        result->phase_ns[i] = (unsigned)(ticks[i] * 1000000000ULL /
                            ((uint64_t)COUNTS_PER_SECOND * NTT_SAMPLES));
    return memcmp(ntt_selected, ntt_reference, sizeof ntt_selected);
}
#endif

/* Same starting SHAKE state, same byte stream. The timer surrounds just
 * the full-block calls, including the copies and cache work in the driver. */
static unsigned time_squeeze(keccak_state *state, uint8_t *out)
{
    XTime begin, end;
    XTime_GetTime(&begin);
    for (unsigned i = 0; i < SQUEEZE_REPEATS; ++i)
        shake128_squeezeblocks(out, SQUEEZE_BLOCKS, state);
    XTime_GetTime(&end);
    return (unsigned)((end - begin) * 1000000000ULL /
                      ((uint64_t)(COUNTS_PER_SECOND) * SQUEEZE_REPEATS));
}

static int check_squeeze(void)
{
    keccak_state reference, target;
    uint8_t expected[SQUEEZE_BLOCKS * SHAKE128_RATE], out[sizeof expected];
    benchmark_use_fpga = 0;
    shake128_absorb_once(&reference, message, TEXT_BYTES);
    target = reference;
    squeeze_ref_ns = time_squeeze(&reference, expected);
    benchmark_use_fpga = MLDSA_BACKEND != 0;
    squeeze_target_ns = time_squeeze(&target, out);
    return memcmp(expected, out, sizeof out) || memcmp(reference.s, target.s, sizeof target.s);
}

/* Mean time of one call, in microseconds, over all the messages. */
static unsigned mean_us(XTime begin, XTime end)
{
    return (unsigned)((end - begin) * 1000000ULL /
                      ((uint64_t)(COUNTS_PER_SECOND) * MESSAGES));
}
#endif

/* The build renames the reference's randombytes symbol to this benchmark fixture. */
static uint8_t next_byte;
void mldsa_benchmark_bytes(uint8_t *out, size_t len)
{
    while (len--) *out++ = next_byte++;
}

static void select_message(unsigned i)
{
    for (unsigned k = 0; k < 4; ++k)
        message[TEXT_BYTES + k] = (uint8_t)(i >> (8 * k));
}

static int sign_message(unsigned i, uint8_t *sig)
{
    size_t len = 0;
    select_message(i);
    next_byte = 128; /* Same signing coins for every message and both paths. */
    return crypto_sign_signature(sig, &len, message, sizeof message,
                                 context, sizeof context - 1, sk) || len != CRYPTO_BYTES;
}

static int verify_message(unsigned i, const uint8_t *sig)
{
    select_message(i);
    return crypto_sign_verify(sig, CRYPTO_BYTES, message, sizeof message,
                              context, sizeof context - 1, pk);
}

/* Two timed blocks: sign every message, then verify every signature. */
static int sign_and_verify_all(uint8_t (*sigs)[CRYPTO_BYTES], int sign_error, int verify_error,
                               unsigned *mean_sign_us, unsigned *mean_verify_us)
{
#ifndef HOST_MODEL
    XTime t0, t1, t2;
    XTime_GetTime(&t0);
#endif
    for (unsigned i = 0; i < MESSAGES; ++i)
        if (sign_message(i, sigs[i])) return sign_error;
#ifndef HOST_MODEL
    XTime_GetTime(&t1);
#endif
    for (unsigned i = 0; i < MESSAGES; ++i)
        if (verify_message(i, sigs[i])) return verify_error;
#ifndef HOST_MODEL
    XTime_GetTime(&t2);
    *mean_sign_us = mean_us(t0, t1);
    *mean_verify_us = mean_us(t1, t2);
#else
    (void)mean_sign_us;
    (void)mean_verify_us;
#endif
    return 0;
}

static int run(void)
{
    if (crypto_sign_keypair(pk, sk)) return 1;
    int rc = sign_and_verify_all(reference_signatures, 1, 1, &ref_sign_us, &ref_verify_us);
    if (rc) return rc;

#if MLDSA_BACKEND != 0
    hw_init();
    benchmark_use_fpga = 1;
#endif
#ifndef HOST_MODEL
    if (check_squeeze()) return 5;
#endif
#ifdef MLDSA_NTT
    rc = sign_and_verify_all(signatures, 2, 3, &keccak_sign_us, &keccak_verify_us);
    if (rc) return rc;
    if (memcmp(signatures, reference_signatures, sizeof signatures)) return 3;
    benchmark_use_ntt = 1;
#ifndef HOST_MODEL
    if (check_ntt(0, &forward)) return 6;
#endif
#ifdef NTT_BIDIRECTIONAL
    rc = sign_and_verify_all(signatures, 2, 3, &forward_sign_us, &forward_verify_us);
    if (rc) return rc;
    if (memcmp(signatures, reference_signatures, sizeof signatures)) return 3;
    benchmark_use_invntt = 1;
#ifndef HOST_MODEL
    if (check_ntt(1, &inverse)) return 7;
#endif
#endif
#endif
    rc = sign_and_verify_all(signatures, 2, 3, &sign_us, &verify_us);
    if (rc) return rc;
    if (memcmp(signatures, reference_signatures, sizeof signatures)) return 3;

    message[0] ^= 1;
    int rejected = verify_message(0, signatures[0]) != 0;
    message[0] ^= 1;
    return rejected ? 0 : 4;
}

int main(void)
{
    int rc = run();
    do {
#ifdef NTT_BIDIRECTIONAL
        report("Keccak v3 + bidirectional NTT (64-bit)\r\n");
#elif defined(MLDSA_NTT)
        report("Keccak v3 + forward NTT\r\n");
#endif
        report("ML-DSA mode %d, backend %d, %d messages: %s (code %d)\r\n",
               DILITHIUM_MODE, MLDSA_BACKEND, MESSAGES, rc ? "FAIL" : "PASS", rc);
#ifdef HOST_MODEL
        return rc;
#else
        if (!rc) {
            report("SHAKE128 5 blocks (ns/call, 1000 calls): SW %u, selected %u\r\n",
                   squeeze_ref_ns, squeeze_target_ns);
            report("SW mean sign %u us, verify %u us\r\n", ref_sign_us, ref_verify_us);
#ifdef MLDSA_NTT
            report("NTT forward (ns/call, %d calls): SW %u, FPGA %u\r\n",
                   NTT_SAMPLES, forward.ref_ns, forward.target_ns);
            report("Keccak only mean sign %u us, verify %u us\r\n", keccak_sign_us, keccak_verify_us);
            report("NTT phases (ns/call): copy+flush %u, start-to-done %u, invalidate+copy %u\r\n",
                   forward.phase_ns[0], forward.phase_ns[1], forward.phase_ns[2]);
#ifdef NTT_BIDIRECTIONAL
            report("NTT inverse (ns/call, %d calls): SW %u, FPGA %u\r\n",
                   NTT_SAMPLES, inverse.ref_ns, inverse.target_ns);
            report("Inverse phases (ns/call): copy+flush %u, start-to-done %u, invalidate+copy %u\r\n",
                   inverse.phase_ns[0], inverse.phase_ns[1], inverse.phase_ns[2]);
            report("Keccak + forward mean sign %u us, verify %u us\r\n", forward_sign_us, forward_verify_us);
#endif
#endif
            report("selected mean sign %u us, verify %u us\r\n", sign_us, verify_us);
        }
        report("END\r\n");
        sleep(2);
#endif
    } while (1);
}
