#include "ntt_forward.h"

// Twiddle factors in Montgomery representation from pq-crystals/dilithium ref/ntt.c.
static const int32_t zetas[256] = {
         0,    25847, -2608894,  -518909,   237124,  -777960,  -876248,   466468,
   1826347,  2353451,  -359251, -2091905,  3119733, -2884855,  3111497,  2680103,
   2725464,  1024112, -1079900,  3585928,  -549488, -1119584,  2619752, -2108549,
  -2118186, -3859737, -1399561, -3277672,  1757237,   -19422,  4010497,   280005,
   2706023,    95776,  3077325,  3530437, -1661693, -3592148, -2537516,  3915439,
  -3861115, -3043716,  3574422, -2867647,  3539968,  -300467,  2348700,  -539299,
  -1699267, -1643818,  3505694, -3821735,  3507263, -2140649, -1600420,  3699596,
    811944,   531354,   954230,  3881043,  3900724, -2556880,  2071892, -2797779,
  -3930395, -1528703, -3677745, -3041255, -1452451,  3475950,  2176455, -1585221,
  -1257611,  1939314, -4083598, -1000202, -3190144, -3157330, -3632928,   126922,
   3412210,  -983419,  2147896,  2715295, -2967645, -3693493,  -411027, -2477047,
   -671102, -1228525,   -22981, -1308169,  -381987,  1349076,  1852771, -1430430,
  -3343383,   264944,   508951,  3097992,    44288, -1100098,   904516,  3958618,
  -3724342,    -8578,  1653064, -3249728,  2389356,  -210977,   759969, -1316856,
    189548, -3553272,  3159746, -1851402, -2409325,  -177440,  1315589,  1341330,
   1285669, -1584928,  -812732, -1439742, -3019102, -3881060, -3628969,  3839961,
   2091667,  3407706,  2316500,  3817976, -3342478,  2244091, -2446433, -3562462,
    266997,  2434439, -1235728,  3513181, -3520352, -3759364, -1197226, -3193378,
    900702,  1859098,   909542,   819034,   495491, -1613174,   -43260,  -522500,
   -655327, -3122442,  2031748,  3207046, -3556995,  -525098,  -768622, -3595838,
    342297,   286988, -2437823,  4108315,  3437287, -3342277,  1735879,   203044,
   2842341,  2691481, -2590150,  1265009,  4055324,  1247620,  2486353,  1595974,
  -3767016,  1250494,  2635921, -3548272, -2994039,  1869119,  1903435, -1050970,
  -1333058,  1237275, -3318210, -1430225,  -451100,  1312455,  3306115, -1962642,
  -1279661,  1917081, -2546312, -1374803,  1500165,   777191,  2235880,  3406031,
   -542412, -2831860, -1671176, -1846953, -2584293, -3724270,   594136, -3776993,
  -2013608,  2432395,  2454455,  -164721,  1957272,  3369112,   185531, -1207385,
  -3183426,   162844,  1616392,  3014001,   810149,  1652634, -3694233, -1799107,
  -3038916,  3523897,  3866901,   269760,  2213111,  -975884,  1717735,   472078,
   -426683,  1723600, -1803090,  1910376, -1667432, -1104333,  -260646, -3833893,
  -2939036, -2235985,  -420899, -2286327,   183443,  -976891,  1612842, -3545687,
   -554416,  3919660,   -48306, -1362209,  3937738,  1400424,  -846154,  1976782
};

static int32_t montgomery_reduce_product(int64_t product)
{
#pragma HLS INLINE
    const int32_t m = (int32_t)((uint32_t)product * 58728449U);
    return (int32_t)((product - (int64_t)m * 8380417) >> 32);
}

template<unsigned Lane>
static void butterfly(const int32_t values[16], unsigned group, unsigned shift,
                      unsigned stride, unsigned mask, bool inverse, int32_t &sum, int32_t &difference)
{
#pragma HLS INLINE off
#pragma HLS PIPELINE II=2
#pragma HLS ARRAY_PARTITION variable=values complete
    const unsigned index = group * 8 + Lane;
    const unsigned segment = (inverse ? 127 - index : index) >> shift;
    const int32_t zeta = zetas[(128U >> shift) + segment];
    const unsigned lo = ((Lane & ~mask) << 1) | (Lane & mask);
    const int32_t a = values[lo], b = values[lo + stride];
    // For the inverse, zeta * (b-a) equals the reference's (-zeta) * (a-b).
    const int32_t t = montgomery_reduce_product((int64_t)zeta * (inverse ? b - a : b));
    sum = a + (inverse ? b : t);
    difference = inverse ? t : a - t;
}

static void transform_stage(const int32_t input[32][8], int32_t output[32][8], unsigned shift, bool inverse)
{
#pragma HLS INLINE off
    const unsigned distance = 1U << shift;
    const unsigned row_distance = distance >= 8 ? distance / 8 : 1;
    const unsigned row_mask = row_distance - 1;
    const unsigned stride = distance >= 8 ? 8 : distance;
    const unsigned mask = stride - 1;
    // Each group touches two entire rows, disjoint from every other group in this stage.
    groups: for (unsigned group = 0; group < 16; ++group) {
#pragma HLS PIPELINE II=2
        const unsigned row_lo = ((group & ~row_mask) << 1) | (group & row_mask);
        const unsigned row_hi = row_lo + row_distance;
        int32_t values[16], sums[8], differences[8];
#pragma HLS ARRAY_PARTITION variable=values complete
#pragma HLS ARRAY_PARTITION variable=sums complete
#pragma HLS ARRAY_PARTITION variable=differences complete
        read_banks: for (unsigned bank = 0; bank < 8; ++bank) {
#pragma HLS UNROLL
            values[bank] = input[row_lo][bank];
            values[8 + bank] = input[row_hi][bank];
        }
        // Distinct specializations preserve eight physical units across all stages.
        butterfly<0>(values, group, shift, stride, mask, inverse, sums[0], differences[0]);
        butterfly<1>(values, group, shift, stride, mask, inverse, sums[1], differences[1]);
        butterfly<2>(values, group, shift, stride, mask, inverse, sums[2], differences[2]);
        butterfly<3>(values, group, shift, stride, mask, inverse, sums[3], differences[3]);
        butterfly<4>(values, group, shift, stride, mask, inverse, sums[4], differences[4]);
        butterfly<5>(values, group, shift, stride, mask, inverse, sums[5], differences[5]);
        butterfly<6>(values, group, shift, stride, mask, inverse, sums[6], differences[6]);
        butterfly<7>(values, group, shift, stride, mask, inverse, sums[7], differences[7]);
        write_banks: for (unsigned pos = 0; pos < 16; ++pos) {
#pragma HLS UNROLL
            // Removing the stride bit recovers the butterfly lane for this coefficient.
            const unsigned lane = ((pos >> 1) & ~mask) | (pos & mask);
            output[pos < 8 ? row_lo : row_hi][pos % 8] =
                (pos & stride) ? differences[lane] : sums[lane];
        }
    }
}

// 64-bit master: two coefficients per AXI beat, 128 beats per direction
// instead of 256. Coefficients 2i and 2i+1 land in adjacent banks of row i/4.
#ifdef NTT_BIDIRECTIONAL
void ntt_transform(uint64_t coefficients[128], bool inverse)
#else
void ntt_forward(uint64_t coefficients[128])
#endif
{
#ifdef NTT_BIDIRECTIONAL
#pragma HLS INTERFACE mode=s_axilite port=inverse
#else
    const bool inverse = false;
#endif
#pragma HLS INTERFACE mode=m_axi port=coefficients offset=slave bundle=gmem depth=128
#pragma HLS INTERFACE mode=s_axilite port=return
    int32_t local[32][8];
    int32_t scratch[32][8];
#pragma HLS ARRAY_PARTITION variable=local complete dim=2
#pragma HLS ARRAY_PARTITION variable=scratch complete dim=2
#pragma HLS ALLOCATION instances=transform_stage limit=1 function
    load: for (unsigned i = 0; i < 128; ++i) {
#pragma HLS PIPELINE II=1
        const uint64_t word = coefficients[i];
        local[i / 4][2 * (i % 4)] = (int32_t)(uint32_t)word;
        local[i / 4][2 * (i % 4) + 1] = (int32_t)(uint32_t)(word >> 32);
    }
    stage_pairs: for (int shift = 7; shift >= 1; shift -= 2) {
#pragma HLS PIPELINE off
        transform_stage(local, scratch, inverse ? 7 - shift : shift, inverse);
        transform_stage(scratch, local, inverse ? 8 - shift : shift - 1, inverse);
    }
    store: for (unsigned i = 0; i < 128; ++i) {
#pragma HLS PIPELINE II=1
        int32_t lo = local[i / 4][2 * (i % 4)];
        int32_t hi = local[i / 4][2 * (i % 4) + 1];
        if (inverse) {
            // invntt_tomont scales by mont^2/256. Fuse it into the DDR write.
            lo = montgomery_reduce_product((int64_t)41978 * lo);
            hi = montgomery_reduce_product((int64_t)41978 * hi);
        }
        coefficients[i] = (uint32_t)lo | ((uint64_t)(uint32_t)hi << 32);
    }
}
