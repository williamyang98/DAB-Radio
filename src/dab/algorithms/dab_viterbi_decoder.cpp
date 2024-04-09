#include "./dab_viterbi_decoder.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <limits>
#include <memory>
#include "detect_architecture.h"
#include "simd_flags.h" // NOLINT
#include "utility/span.h"
#include "viterbi/viterbi_branch_table.h"
#include "viterbi/viterbi_decoder_config.h"
#include "viterbi/viterbi_decoder_core.h"
#include "viterbi_config.h"

// DOC: ETSI EN 300 401
// Clause 11.1 - Convolutional code
// Clause 11.1.1 - Mother code
// Octal form | Binary form | Reversed binary | Decimal form |
//     133    | 001 011 011 |    110 110 1    |      109     |
//     171    | 001 111 001 |    100 111 1    |       79     |
//     145    | 001 100 101 |    101 001 1    |       83     |
//     133    | 001 011 011 |    110 110 1    |      109     |
constexpr size_t K = DAB_Viterbi_Decoder::m_constraint_length;
constexpr size_t R = DAB_Viterbi_Decoder::m_code_rate;
const uint8_t code_polynomial[R] = { 109, 79, 83, 109 };
constexpr int16_t soft_decision_low = int16_t(SOFT_DECISION_VITERBI_LOW);
constexpr int16_t soft_decision_high = int16_t(SOFT_DECISION_VITERBI_HIGH);
constexpr int16_t soft_decision_unpunctured = int16_t(SOFT_DECISION_VITERBI_PUNCTURED);

// Use same configuration for all decoders
static ViterbiDecoder_Config<uint16_t> create_decoder_config() {
    const uint16_t max_error = uint16_t(soft_decision_high-soft_decision_low) * uint16_t(DAB_Viterbi_Decoder::m_code_rate);
    const uint16_t error_margin = max_error * uint16_t(5u);
    ViterbiDecoder_Config<uint16_t> config;
    config.soft_decision_max_error = max_error;
    config.initial_start_error = std::numeric_limits<uint16_t>::min();
    config.initial_non_start_error = config.initial_start_error + error_margin;
    config.renormalisation_threshold = std::numeric_limits<uint16_t>::max() - error_margin;
    return config; 
}
static const auto decoder_config = create_decoder_config();

// Share the branch table for all decoders
// This saves memory since we don't reallocate the same table for each decoder instance
static const auto decoder_branch_table = ViterbiBranchTable<K,R,int16_t>(
    code_polynomial,
    soft_decision_high, soft_decision_low
);

// Wrap compile time selected decoder for forward declaration
#if defined(__ARCH_X86__)
    #if defined(__AVX2__)
        #pragma message("DAB_VITERBI_DECODER using x86 AVX2")
        #include "viterbi/x86/viterbi_decoder_avx_u16.h"
        using Decoder = ViterbiDecoder_AVX_u16<K,R>;
    #elif defined(__SSE4_1__)
        #pragma message("DAB_VITERBI_DECODER using x86 SSE4.1")
        #include "viterbi/x86/viterbi_decoder_sse_u16.h"
        using Decoder = ViterbiDecoder_SSE_u16<K,R>;
    #else
        #pragma message("DAB_VITERBI_DECODER using x86 SCALAR")
        #include "viterbi/viterbi_decoder_scalar.h"
        using Decoder = ViterbiDecoder_Scalar<K,R,uint16_t,int16_t>;
    #endif
#elif defined(__ARCH_AARCH64__)
    #pragma message("DAB_VITERBI_DECODER using ARM AARCH64 NEON")
    #include "viterbi/arm/viterbi_decoder_neon_u16.h"
    using Decoder = ViterbiDecoder_NEON_u16<K,R>;
#else
    #pragma message("DAB_VITERBI_DECODER using crossplatform SCALAR")
    #include "viterbi/viterbi_decoder_scalar.h"
    using Decoder = ViterbiDecoder_Scalar<K,R,uint16_t,int16_t>;
#endif

using Core = ViterbiDecoder_Core<K,R,uint16_t,int16_t>;
class DAB_Viterbi_Decoder_Internal: public Core
{
public:
    template <typename ... U>
    DAB_Viterbi_Decoder_Internal(U&& ... args): Core(std::forward<U>(args)...) {}
};


DAB_Viterbi_Decoder::DAB_Viterbi_Decoder()
: m_depunctured_symbols(), m_accumulated_error(0)
{
    m_decoder = std::make_unique<DAB_Viterbi_Decoder_Internal>(
        decoder_branch_table,
        decoder_config
    );
}

DAB_Viterbi_Decoder::~DAB_Viterbi_Decoder() {

}

void DAB_Viterbi_Decoder::set_traceback_length(const size_t traceback_length) {
    m_decoder->set_traceback_length(traceback_length);
}

size_t DAB_Viterbi_Decoder::get_traceback_length() const {
    return m_decoder->get_traceback_length();
}

size_t DAB_Viterbi_Decoder::get_current_decoded_bit() const {
    return m_decoder->m_current_decoded_bit;
};

void DAB_Viterbi_Decoder::reset(const size_t starting_state) {
    m_decoder->reset(starting_state);
    m_accumulated_error = 0;
}

size_t DAB_Viterbi_Decoder::update(
    tcb::span<const viterbi_bit_t> punctured_symbols,
    tcb::span<const uint8_t> puncture_code,
    const size_t requested_output_symbols
) {
    const auto res = depuncture_symbols(punctured_symbols, puncture_code, requested_output_symbols);
    m_accumulated_error += Decoder::update<uint64_t>(*m_decoder.get(), m_depunctured_symbols.data(), res.total_output_symbols);
    return res.total_punctured_symbols;
}

uint64_t DAB_Viterbi_Decoder::chainback(tcb::span<uint8_t> bytes_out, const size_t end_state) {
    const size_t total_bits = bytes_out.size()*8u;
    m_decoder->chainback(bytes_out.data(), total_bits, end_state);
    const uint64_t error = m_accumulated_error + uint64_t(m_decoder->get_error());
    return error;
}

DAB_Viterbi_Decoder::depuncture_res DAB_Viterbi_Decoder::depuncture_symbols(
    tcb::span<const viterbi_bit_t> punctured_symbols, 
    tcb::span<const uint8_t> puncture_code,
    const size_t requested_output_symbols
) {
    assert(requested_output_symbols % m_code_rate == 0);

    const size_t total_punctured_symbols = punctured_symbols.size();
    const size_t total_puncture_code = puncture_code.size();

    // Resize only if we need more depunctured symbols
    if (requested_output_symbols > m_depunctured_symbols.size()) {
        m_depunctured_symbols.resize(requested_output_symbols);
    }

    depuncture_res res;
    res.total_output_symbols = 0;
    res.total_punctured_symbols = 0;

    size_t index_punctured_symbol = 0;
    size_t index_puncture_code = 0;
    size_t index_output_symbol = 0;

    while (index_output_symbol < requested_output_symbols) {
        const size_t total_block_punctured = size_t(puncture_code[index_puncture_code]);
        const size_t total_block_unpunctured = m_code_rate - size_t(total_block_punctured);

        const size_t remaining_punctured = total_punctured_symbols - index_punctured_symbol;
        assert(remaining_punctured >= total_block_punctured);
        if (remaining_punctured < total_block_punctured) { 
            return res;
        }

        for (size_t i = 0; i < total_block_punctured; i++)  {
            m_depunctured_symbols[index_output_symbol] = int16_t(punctured_symbols[index_punctured_symbol]);
            index_punctured_symbol++;
            index_output_symbol++;
        }

        for (size_t i = 0; i < total_block_unpunctured; i++)  {
            m_depunctured_symbols[index_output_symbol] = soft_decision_unpunctured;
            index_output_symbol++;
        }

        index_puncture_code = (index_puncture_code+1) % total_puncture_code;
    }

    res.total_output_symbols = index_output_symbol;
    res.total_punctured_symbols = index_punctured_symbol;
    return res;
}
