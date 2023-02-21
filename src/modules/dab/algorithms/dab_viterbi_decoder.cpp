#include "dab_viterbi_decoder.h"
#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include <limits>

#include "viterbi/viterbi_branch_table.h"
#include "viterbi/viterbi_decoder_core.h"
#include "viterbi/viterbi_decoder_scalar.h"
#include "viterbi/viterbi_decoder_sse_u16.h"
#include "viterbi/viterbi_decoder_avx_u16.h"
#include "viterbi_config.h"

// DOC: ETSI EN 300 401
// Clause 11.1 - Convolutional code
// Clause 11.1.1 - Mother code
// Octal form | Binary form | Reversed binary | Decimal form |
//     133    | 001 011 011 |    110 110 1    |      109     |
//     171    | 001 111 001 |    100 111 1    |       79     |
//     145    | 001 100 101 |    101 001 1    |       83     |
//     133    | 001 011 011 |    110 110 1    |      109     |
const uint8_t code_polynomial[DAB_Viterbi_Decoder::code_rate] = { 109, 79, 83, 109 };
constexpr int16_t soft_decision_low = int16_t(SOFT_DECISION_VITERBI_LOW);
constexpr int16_t soft_decision_high = int16_t(SOFT_DECISION_VITERBI_HIGH);
constexpr int16_t soft_decision_unpunctured = int16_t(SOFT_DECISION_VITERBI_PUNCTURED);

// Use same configuration for all decoders
ViterbiDecoder_Config<uint16_t> create_decoder_config() {
    const uint16_t max_error = uint16_t(soft_decision_high-soft_decision_low) * uint16_t(DAB_Viterbi_Decoder::code_rate);
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
static const auto decoder_branch_table = ViterbiBranchTable<int16_t>(
    DAB_Viterbi_Decoder::constraint_length, 
    DAB_Viterbi_Decoder::code_rate, 
    code_polynomial,
    soft_decision_high, soft_decision_low,
    32u
);

// Wrap compile time selected decoder for forward declaration
#if defined(__AVX2__)
#pragma message("Compiling viterbi decoder with AVX2")
using ExternalDecoder = ViterbiDecoder_AVX_u16<uint64_t>;
#elif defined(__SSSE3__)
#pragma message("Compiling viterbi decoder with SSSE3")
using ExternalDecoder = ViterbiDecoder_SSE_u16<uint64_t>;
#else
#pragma message("Compiling viterbi decoder with scalar instructions")
using ExternalDecoder = ViterbiDecoder_Scalar<uint16_t,int16_t,uint64_t,uint64_t>;
#endif

class DAB_Viterbi_Decoder_Internal: public ExternalDecoder 
{
public:
    template <typename ... U>
    DAB_Viterbi_Decoder_Internal(U&& ... args)
    : ExternalDecoder(std::forward<U>(args)...) {}
};


DAB_Viterbi_Decoder::DAB_Viterbi_Decoder()
: depunctured_symbols(code_rate) 
{
    decoder = std::make_unique<DAB_Viterbi_Decoder_Internal>(
        decoder_branch_table,
        decoder_config
    );
}

DAB_Viterbi_Decoder::~DAB_Viterbi_Decoder() {

}

void DAB_Viterbi_Decoder::set_traceback_length(const size_t traceback_length) {
    decoder->set_traceback_length(traceback_length);
}

size_t DAB_Viterbi_Decoder::get_traceback_length() const {
    return decoder->get_traceback_length();
}

size_t DAB_Viterbi_Decoder::get_current_decoded_bit() const {
    return decoder->get_current_decoded_bit();
};

void DAB_Viterbi_Decoder::reset(const size_t starting_state) {
    decoder->reset(starting_state);
}

size_t DAB_Viterbi_Decoder::update(
    tcb::span<const int8_t> punctured_symbols,
    tcb::span<const uint8_t> puncture_code,
    const size_t requested_output_symbols
) {
    const size_t total_symbols = punctured_symbols.size();
    auto& decoder_ref = *(decoder.get());

    size_t index_punctured_symbol = 0;
    size_t index_puncture_code = 0;
    size_t index_output_symbol = 0;
    while (index_output_symbol < requested_output_symbols) {
        for (size_t i = 0u; i < code_rate; i++) {
            int16_t& v = depunctured_symbols[i];
            const bool is_punctured = puncture_code[index_puncture_code];
            if (is_punctured) {
                // NOTE: If our puncture code is invalid or we request too many symbols
                //       we may expect a punctured symbol when there isn't one
                //       Ideally this is caught during development but as a failsafe we exit early
                assert(index_punctured_symbol < total_symbols);
                if (index_punctured_symbol >= total_symbols) { 
                    return index_punctured_symbol;
                }
                v = int16_t(punctured_symbols[index_punctured_symbol]);
                index_punctured_symbol++;
            } else {
                v = soft_decision_unpunctured;
            }
            index_puncture_code = (index_puncture_code+1) % puncture_code.size();
            index_output_symbol++;
        }

        decoder_ref.update(depunctured_symbols.data(), code_rate);
    }

    return index_punctured_symbol;
}

uint64_t DAB_Viterbi_Decoder::chainback(tcb::span<uint8_t> bytes_out, const size_t end_state) {
    const size_t total_bits = bytes_out.size()*8u;
    return decoder->chainback(bytes_out.data(), total_bits, end_state);
}