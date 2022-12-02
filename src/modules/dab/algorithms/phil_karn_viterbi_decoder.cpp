/* Generic Viterbi decoder,
 * Copyright Phil Karn, KA9Q,
 * Code has been slightly modified for use with Spiral (www.spiral.net)
 * Karn's original code can be found here: http://www.ka9q.net/code/fec/
 * May be used under the terms of the GNU Lesser General Public License (LGPL)
 * see http://www.gnu.org/copyleft/lgpl.html
 */

#include <stdint.h>
#include <limits.h>
#include <math.h>

#include <memory.h>
// Useful description for which headers include which intrinsics
// https://stackoverflow.com/questions/11228855/header-files-for-x86-simd-intrinsics
#include <immintrin.h>

#include "phil_karn_viterbi_decoder.h"

#ifdef _MSC_VER
#define ALIGNED(x) __declspec(align(x))
#else
#define ALIGNED(x) __attribute__ ((aligned(x)))
#endif

#ifdef _WIN32
#define posix_memalign(p, a, s) (((*(p)) = _aligned_malloc((s), (a))), *(p) ? 0 : errno)
#define posix_free(a) _aligned_free(a)
#else
#define posix_free(a) free(a)
#endif


#define K CONSTRAINT_LENGTH
#define NUMSTATES (1 << (K-1))

/* ADDSHIFT and SUBSHIFT make sure that the thing returned is a byte. */
#if ((K-1) < 8)
#define ADDSHIFT (8 - (K-1))
#define SUBSHIFT 0
#elif ((K-1) > 8)
#define ADDSHIFT 0
#define SUBSHIFT ((K-1) - 8)
#else
#define ADDSHIFT 0
#define SUBSHIFT 0
#endif

// Bytes to align to for intrinsic
#define ALIGN_AMOUNT sizeof(__m256i)

uint8_t* CreateParityTable() {
    const int N = 256;
    uint8_t* table = new uint8_t[N];
    for (int i = 0; i < N; i++) {
        uint8_t parity = 0;
        uint8_t b = static_cast<uint8_t>(i);
        for (int j = 0; j < 8; j++) {
            parity ^= (b & 0b1);
            b = b >> 1;
        }
        table[i] = parity;
    }
    return table;
};

uint8_t* CreateBitCountTable() {
    const int N = 256;
    uint8_t* table = new uint8_t[N];
    for (int i = 0; i < N; i++) {
        uint8_t bitcount = 0;
        uint8_t b = static_cast<uint8_t>(i);
        for (int j = 0; j < 8; j++) {
            bitcount += (b & 0b1);
            b = b >> 1;
        }
        table[i] = bitcount;
    }
    return table;
}

uint8_t* CreateBitReverseTable() {
    const int N = 256;
    uint8_t* table = new uint8_t[N];
    for (int i = 0; i < N; i++) {
        uint8_t b = static_cast<uint8_t>(i);
        b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
        b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
        b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
        table[i] = b;
    }
    return table;
}

const static uint8_t* ParityTable = CreateParityTable();
const static uint8_t* BitcountTable = CreateBitCountTable();
const static uint8_t* BitReverseTable = CreateBitReverseTable();

// decision_t is a BIT vector
typedef ALIGNED(ALIGN_AMOUNT) union {
    DECISIONTYPE buf[NUMSTATES/DECISIONTYPE_BITSIZE];
    uint32_t buf32[NUMSTATES/32];
    uint16_t buf16[NUMSTATES/16];
    uint8_t  buf8 [NUMSTATES/8 ];
} decision_t;

typedef ALIGNED(ALIGN_AMOUNT) union {
    COMPUTETYPE buf[NUMSTATES];
    __m128i b128[8];
    __m256i b256[4];
} metric_t;

struct vitdec_t {
    ALIGNED(ALIGN_AMOUNT) metric_t metrics1;
    ALIGNED(ALIGN_AMOUNT) metric_t metrics2;

    union ALIGNED(ALIGN_AMOUNT) {
        COMPUTETYPE buf[32];
        __m128i b128[4];
        __m256i b256[2];
    } BranchTable[CODE_RATE];

    metric_t* old_metrics; 
    metric_t* new_metrics; 
    decision_t *decisions;  

    int maximum_decoded_bits;
    int curr_decoded_bit;

    COMPUTETYPE soft_decision_max_error;
};

static inline uint8_t parityb(const uint8_t x) {
    return ParityTable[x];
}

static inline uint8_t parity(uint32_t x) {
    /* Fold down to one byte */
    x ^= (x >> 16);
    x ^= (x >> 8);
    return parityb(x);
}

inline void renormalize(COMPUTETYPE *x, COMPUTETYPE threshold) {
    if (x[0] > threshold) {
        COMPUTETYPE min = x[0];
        for (int i = 0; i < NUMSTATES; i++) {
            if (min > x[i]) {
                min = x[i];
            }
        }
        for (int i = 0; i < NUMSTATES; i++) {
            x[i] -= min;
        }
    }
}

/* Initialize Viterbi decoder for start of new frame */
void init_viterbi(vitdec_t* vp, int starting_state) {
    // Give initial error to all states
    for (int i = 0; i < NUMSTATES; i++) {
        vp->metrics1.buf[i] = INITIAL_NON_START_ERROR;
    }
    vp->old_metrics = &vp->metrics1;
    vp->new_metrics = &vp->metrics2;

    // Only the starting state has 0 error
    vp->old_metrics->buf[starting_state & (NUMSTATES-1)] = INITIAL_START_ERROR;
    vp->curr_decoded_bit = 0;
    {
        const int N = vp->maximum_decoded_bits;
        decision_t* d = vp->decisions;
        memset(d, 0, N*sizeof(decision_t));
    }
}

/* Create a new instance of a Viterbi decoder */
vitdec_t* create_viterbi(
    const uint8_t polys[CODE_RATE], const int len,
    COMPUTETYPE soft_decision_high, COMPUTETYPE soft_decision_low) 
{
    vitdec_t* vp;
    if (posix_memalign((void**)&vp, ALIGN_AMOUNT, sizeof(vitdec_t))) {
        return NULL;
    }

    const int nb_padding_bits = (K-1);
    const int nb_max_input_bits = len+nb_padding_bits;
    if (posix_memalign((void**)&vp->decisions, ALIGN_AMOUNT, nb_max_input_bits*sizeof(decision_t))) {
        posix_free(vp);
        return NULL;
    }

    vp->maximum_decoded_bits = nb_max_input_bits;
    vp->curr_decoded_bit = 0;
    for (int state = 0; state < NUMSTATES/2; state++) {
        for (int i = 0; i < CODE_RATE; i++) {
            const int v = parity((state << 1) & polys[i]);
            vp->BranchTable[i].buf[state] = v ? soft_decision_high : soft_decision_low;
        }
    }
    vp->soft_decision_max_error = soft_decision_high - soft_decision_low;
    init_viterbi(vp, 0);
    return vp;
}

void delete_viterbi(vitdec_t* vp) {
    if (vp != NULL) {
        posix_free(vp->decisions);
        posix_free(vp);
    }
}

COMPUTETYPE get_error_viterbi(vitdec_t* vp, const int state) {
    return vp->old_metrics->buf[state % NUMSTATES];
}

void chainback_viterbi(
    vitdec_t* const vp, unsigned char *data, 
    const unsigned int nbits, const unsigned int endstate)
{
    decision_t* d = vp->decisions;

    // ignore the tail bits
    d += (K-1);

    const int nb_type_bits = DECISIONTYPE_BITSIZE;
    unsigned int curr_state = endstate;
    curr_state = (curr_state % NUMSTATES) << ADDSHIFT;

    for (int i = nbits-1; i >= 0; i--) {
        const int curr_buf_index = (curr_state >> ADDSHIFT) / nb_type_bits;
        const int curr_buf_bit   = (curr_state >> ADDSHIFT) % nb_type_bits;
        const int input = (d[i].buf[curr_buf_index] >> curr_buf_bit) & 0b1;
        curr_state = (curr_state >> 1) | (input << (K-2+ADDSHIFT));
        // data[i/8] = BitReverseTable[curr_state >> SUBSHIFT];
        data[i/8] = (curr_state >> SUBSHIFT);
    }
}

/* C-language butterfly */
inline void BFLY(int i, int s, const COMPUTETYPE *syms, vitdec_t *vp, decision_t *d) {
    COMPUTETYPE metric = 0;
    COMPUTETYPE m0, m1, m2, m3;
    int decision0, decision1;

    for (int j = 0; j < CODE_RATE; j++) {
        auto& sym = syms[s*CODE_RATE + j];
        // XOR difference (only works for positive integers)
        // COMPUTETYPE error = (vp->BranchTable[j].buf[i] ^ sym) >> METRICSHIFT;
        // Absolute difference 
        COMPUTETYPE error = vp->BranchTable[j].buf[i] - sym;
        error = (error > 0) ? error : -error;
        metric += error >> METRICSHIFT;
    }
    metric = metric >> PRECISIONSHIFT;

    const COMPUTETYPE max = ((CODE_RATE * (vp->soft_decision_max_error >> METRICSHIFT)) >> PRECISIONSHIFT);

    m0 = vp->old_metrics->buf[i] + metric;
    m1 = vp->old_metrics->buf[i+NUMSTATES/2] + (max-metric);
    m2 = vp->old_metrics->buf[i] + (max-metric);
    m3 = vp->old_metrics->buf[i+NUMSTATES/2] + metric;

    decision0 = (signed int)(m0 - m1) > 0;
    decision1 = (signed int)(m2 - m3) > 0;

    vp->new_metrics->buf[2*i]   = decision0 ? m1 : m0;
    vp->new_metrics->buf[2*i+1] = decision1 ? m3 : m2;

    // We push the decision bits into the decision buffer
    const DECISIONTYPE decisions = decision0 | (decision1 << 1);
    const int nb_decision_bits = 2;
    const int buf_type_bits = DECISIONTYPE_BITSIZE;
    const int curr_bit = nb_decision_bits * i;
    const int curr_buf_index = curr_bit / buf_type_bits;
    const int curr_buf_bit = curr_bit % buf_type_bits;
    d->buf[curr_buf_index] |= (decisions << curr_buf_bit);
}

void update_viterbi_blk_scalar(vitdec_t* vp, const COMPUTETYPE *syms, const int nbits) {
    decision_t* d = &vp->decisions[vp->curr_decoded_bit];

    for (int s = 0; s < nbits; s++) {
        for (int i = 0; i < NUMSTATES/2; i++) {
            BFLY(i, s, syms, vp, d);
        }
        renormalize(vp->new_metrics->buf, RENORMALIZE_THRESHOLD);

        d++;
        vp->curr_decoded_bit++;

        metric_t* tmp = vp->old_metrics;
        vp->old_metrics = vp->new_metrics;
        vp->new_metrics = tmp;
    }
}

// Source: https://github.com/zleffke/libfec
// Refer to files viterbi27.c, etc...
// Essentially just an SSSE3 version of update_viterbi_blk_scalar
// NOTE: This ssse3 code is tightly coupled to the constraint length (K) and code rate (L)
//       It is also dependent on the datatype of the metric called COMPUTETYPE
// The following code was designed for K=7, L=4, COMPUTETYPE=int16_t
#ifdef __SSSE3__
void update_viterbi_blk_ssse3(vitdec_t* vp, const COMPUTETYPE* syms, const int nbits) {
    decision_t *d = &vp->decisions[vp->curr_decoded_bit];

    for (int curr_bit = 0; curr_bit < nbits; curr_bit++) {
        /* Splat the 0th symbol across sym0v, the 1st symbol across sym1v, etc */
        __m128i sym[CODE_RATE];
        for (int i = 0; i < CODE_RATE; i++) {
            sym[i] = _mm_set1_epi16(syms[i]);
        }
        syms += CODE_RATE;

        // Step 1: Butterfly algorithm
        /* SSE2 doesn't support saturated adds on unsigned shorts, so we have to use signed shorts */
        for (int i = 0; i < 4; i++) {
            /* Form branch metrics
             * Because BranchTable takes on values 0 and 255, and the values of sym?v are offset binary in the range 0-255,
             * the XOR operations constitute conditional negation.
             * metric and m_metric (-metric) are in the range 0-1530
             */
            __m128i branch_errors[CODE_RATE];
            for (int j = 0; j < CODE_RATE; j++) {
                // Method 1: Calculate error as the XOR of A and B
                // __m128i error = _mm_xor_si128(vp->BranchTable[j].b128[i], sym[j]);
                // Method 2: Calculate error as absolute of difference between A and B
                __m128i error = _mm_subs_epi16(vp->BranchTable[j].b128[i], sym[j]);
                error = _mm_abs_epi16(error);
                branch_errors[j] = error;
            }
            __m128i metric = _mm_set1_epi16(0);
            for (int j = 0; j < CODE_RATE; j++) {
                metric = _mm_add_epi16(metric, branch_errors[j]);
            }

            const COMPUTETYPE max = ((CODE_RATE*(vp->soft_decision_max_error >> METRICSHIFT)) >> PRECISIONSHIFT);
            __m128i m_metric = _mm_sub_epi16(_mm_set1_epi16(max), metric);

            /* Add branch metrics to path metrics */
            __m128i m0 = _mm_adds_epi16(vp->old_metrics->b128[i], metric);
            __m128i m1 = _mm_adds_epi16(vp->old_metrics->b128[4 + i], m_metric);
            __m128i m2 = _mm_adds_epi16(vp->old_metrics->b128[i], m_metric);
            __m128i m3 = _mm_adds_epi16(vp->old_metrics->b128[4 + i], metric);

            /* Compare and select */
            __m128i survivor0 = _mm_min_epi16(m0, m1);
            __m128i survivor1 = _mm_min_epi16(m2, m3);
            __m128i decision0 = _mm_cmpeq_epi16(survivor0, m1);
            __m128i decision1 = _mm_cmpeq_epi16(survivor1, m3);

            /* Pack each set of decisions into 8 8-bit bytes, then interleave them and compress into 16 bits */
            // https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html#text=_mm_packs_epi16&ig_expand=5136
            // _mm_packs_epi16(a,b) = Converts packed 16-bit from a and b to packed 8-bit using signed saturation
            // a is saturated to 8bits then placed at bits 63:0
            // b is saturated to 8bits then placed at bits 127:64
            // https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html#text=_mm_unpacklo_epi8
            // _mm_unpacklo_epi8(a,b) = Unpacks and interleave 8-bit integers from the low half of a and b.
            // c[0:127] = a[0:7],b[0:7],a[8:15],b[8:15],...,a[56:63],b[56:63]
            // https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html#text=_mm_movemask_epi8
            // _mm_movemask_epi8(a) = Returns a mask of the most significant bit of each element in a.
            // c[0:15] = a[7],a[15],a[23],...,a[127]
            d->buf16[i] = _mm_movemask_epi8(_mm_unpacklo_epi8(
                _mm_packs_epi16(decision0, _mm_setzero_si128()), 
                _mm_packs_epi16(decision1, _mm_setzero_si128())));

            /* Store surviving metrics */
            // https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html#text=_mm_unpacklo_epi16&ig_expand=7385
            // _mm_unpacklo_epi16(a,b) = Unpacks and interleave 16-bit integers from the low half of a and b.
            // c[0:127] = a[0:15],b[0:15],a[16:32],b[16:31],...,a[48:63],b[48:63]
            vp->new_metrics->b128[2*i] = _mm_unpacklo_epi16(survivor0, survivor1);
            vp->new_metrics->b128[2*i+1] = _mm_unpackhi_epi16(survivor0, survivor1);
        }

        // Step 2: Renormalisation
        if (vp->new_metrics->buf[0] >= RENORMALIZE_THRESHOLD) {
            union { __m128i v; COMPUTETYPE w[8]; } reduce_buffer;
            /* Find smallest metric and set adjustv to bring it down to SHRT_MIN */
            __m128i adjustv = vp->new_metrics->b128[0];
            for (int i = 1; i < 8; i++) {
                adjustv = _mm_min_epi16(adjustv, vp->new_metrics->b128[i]);
            }

            // Shift half of the array onto the other half and get the minimum between them
            // Repeat this until we get the minimum value of all 16bit values
            adjustv = _mm_min_epi16(adjustv, _mm_srli_si128(adjustv, 8));
            adjustv = _mm_min_epi16(adjustv, _mm_srli_si128(adjustv, 4));
            adjustv = _mm_min_epi16(adjustv, _mm_srli_si128(adjustv, 2));

            reduce_buffer.v = adjustv;
            COMPUTETYPE adjust = reduce_buffer.w[0] - SHRT_MIN;
            adjustv = _mm_set1_epi16(adjust);

            /* We cannot use a saturated subtract, because we often have to adjust by more than SHRT_MAX
             * This is okay since it can't overflow anyway
             */
            for (int i = 0; i < 8; i++) {
                vp->new_metrics->b128[i] = _mm_sub_epi16(vp->new_metrics->b128[i], adjustv);
            }
        }

        // Step 3: Move to the next codeword
        d++;
        vp->curr_decoded_bit++;

        // Step 4: Swap metrics
        metric_t* tmp = vp->old_metrics;
        vp->old_metrics = vp->new_metrics;
        vp->new_metrics = tmp;
    }
}
#endif

// This is the AVX2 version of the SSE2 code
// Could run up to 2x faster
#ifdef __AVX2__
void update_viterbi_blk_avx2(vitdec_t* vp, const COMPUTETYPE* syms, const int nbits) {
    decision_t *d = &vp->decisions[vp->curr_decoded_bit];

    for (int curr_bit = 0; curr_bit < nbits; curr_bit++) {
        /* Splat the 0th symbol across sym0v, the 1st symbol across sym1v, etc */
        __m256i sym[CODE_RATE];
        for (int i = 0; i < CODE_RATE; i++) {
            sym[i] = _mm256_set1_epi16(syms[i]);
        }
        syms += CODE_RATE;

        // Step 1: Butterfly algorithm
        for (int i = 0; i < 2; i++) {
            /* Form branch metrics */
            __m256i branch_errors[CODE_RATE];
            for (int j = 0; j < CODE_RATE; j++) {
                __m256i error = _mm256_subs_epi16(vp->BranchTable[j].b256[i], sym[j]);
                error = _mm256_abs_epi16(error);
                branch_errors[j] = error;
            }

            __m256i metric = _mm256_set1_epi16(0);
            for (int j = 0; j < CODE_RATE; j++) {
                metric = _mm256_add_epi16(metric, branch_errors[j]);
            }

            const COMPUTETYPE max = ((CODE_RATE*(vp->soft_decision_max_error >> METRICSHIFT)) >> PRECISIONSHIFT);
            __m256i m_metric = _mm256_sub_epi16(_mm256_set1_epi16(max), metric);

            /* Add branch metrics to path metrics */
            __m256i m0 = _mm256_adds_epi16(vp->old_metrics->b256[i], metric);
            __m256i m1 = _mm256_adds_epi16(vp->old_metrics->b256[2 + i], m_metric);
            __m256i m2 = _mm256_adds_epi16(vp->old_metrics->b256[i], m_metric);
            __m256i m3 = _mm256_adds_epi16(vp->old_metrics->b256[2 + i], metric);

            /* Compare and select */
            __m256i survivor0 = _mm256_min_epi16(m0, m1);
            __m256i survivor1 = _mm256_min_epi16(m2, m3);
            __m256i decision0 = _mm256_cmpeq_epi16(survivor0, m1);
            __m256i decision1 = _mm256_cmpeq_epi16(survivor1, m3);

            /* Pack each set of decisions into 8 8-bit bytes, then interleave them and compress into 16 bits */
            // 256bit packs works with 128bit segments
            // 256bit unpack works with 128bit segments
            // | = 128bit boundary
            // packs_16  : d0 .... 0 .... | d0 .... 0 ....
            // packs_16  : d1 .... 0 .... | d1 .... 0 ....
            // unpacklo_8: d0 d1 d0 d1 .. | d0 d1 d0 d1 ..
            // movemask_8: b0 b1 b0 b1 .. (256bit/8bit = 32bitmask)
            d->buf32[i] = _mm256_movemask_epi8(_mm256_unpacklo_epi8(
                _mm256_packs_epi16(decision0, _mm256_setzero_si256()), 
                _mm256_packs_epi16(decision1, _mm256_setzero_si256())));

            /* Store surviving metrics */
            // 128bit pack/unpack works on entire 128bit segment
            // | = 128bit boundary, '= lower 64bit boundary
            // survivor0  : s0 ... s0' ... | 
            // survivor1  : s1 ... s1' ... |
            // unpacklo_16: s0' s1'    ... |
            // unpackhi_16: s0  s1     ... |
            // new_metrics: s0  s1     ... | s0' s1'    ... |
            // We effectively interleave survivor0 and survivor1 

            // 256bit pack/unpack works on 128bit segments
            // | = 128bit boundary, '= lower 64bit boundary
            // survivor0  : s0 ... s0' ... | s0" ... s0"' ...
            // survivor1  : s1 ... s1' ... | s1" ... s1"' ...
            // unpacklo_16: s0' s1'    ... | s0"' s1"'    ...
            // unpackhi_16: s0  s1     ... | s0"  s1"     ...
            // new_metrics: s0  s1     ... | s0"  s1"     ... | s0' s1'   ... | s0"' s1"'  ... |
            // This incorrectly interleaves survivor0 and survivor1 
            // Therefore we need to do some reshuffling

            // Helper to get 128bit segments from 256bit intrinsic
            union ALIGNED(ALIGN_AMOUNT) {
                __m256i b256;
                __m128i b128[2];
            } packed_lower, packed_upper;

            packed_lower.b256 = _mm256_unpacklo_epi16(survivor0, survivor1);
            packed_upper.b256 = _mm256_unpackhi_epi16(survivor0, survivor1);

            // Reshuffle into correct order along 128bit boundaries
            vp->new_metrics->b128[4*i+0] = packed_lower.b128[0];
            vp->new_metrics->b128[4*i+1] = packed_upper.b128[0];
            vp->new_metrics->b128[4*i+2] = packed_lower.b128[1];
            vp->new_metrics->b128[4*i+3] = packed_upper.b128[1];
        }

        // Step 2: Renormalisation
        if (vp->new_metrics->buf[0] >= RENORMALIZE_THRESHOLD) {
            union ALIGNED(ALIGN_AMOUNT) { 
                __m256i v; 
                COMPUTETYPE w[4]; 
            } reduce_buffer;
            /* Find smallest metric and set adjustv to bring it down to SHRT_MIN */
            __m256i adjustv = vp->new_metrics->b256[0];
            for (int i = 1; i < 4; i++) {
                adjustv = _mm256_min_epi16(adjustv, vp->new_metrics->b256[i]);
            }

            // Shift half of the array onto the other half and get the minimum between them
            // Repeat this until we get the minimum value of all 16bit values
            adjustv = _mm256_min_epi16(adjustv, _mm256_srli_si256(adjustv, 16));
            adjustv = _mm256_min_epi16(adjustv, _mm256_srli_si256(adjustv, 8));
            adjustv = _mm256_min_epi16(adjustv, _mm256_srli_si256(adjustv, 4));
            adjustv = _mm256_min_epi16(adjustv, _mm256_srli_si256(adjustv, 2));

            reduce_buffer.v = adjustv;
            COMPUTETYPE adjust = reduce_buffer.w[0] - SHRT_MIN;
            adjustv = _mm256_set1_epi16(adjust);

            /* We cannot use a saturated subtract, because we often have to adjust by more than SHRT_MAX
             * This is okay since it can't overflow anyway
             */
            for (int i = 0; i < 4; i++) {
                vp->new_metrics->b256[i] = _mm256_sub_epi16(vp->new_metrics->b256[i], adjustv);
            }
        }

        // Step 3: Move to the next codeword
        d++;
        vp->curr_decoded_bit++;

        // Step 4: Swap metrics
        metric_t* tmp = vp->old_metrics;
        vp->old_metrics = vp->new_metrics;
        vp->new_metrics = tmp;
    }
}
#endif