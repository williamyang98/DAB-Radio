#pragma once

#include <stddef.h>
#include <stdint.h>
#include <memory>
#include <vector>
#include "utility/span.h"

class DAB_Viterbi_Decoder_Internal;

class DAB_Viterbi_Decoder 
{
public:
    static constexpr size_t constraint_length = 7;
    static constexpr size_t code_rate = 4;
private:
    std::unique_ptr<DAB_Viterbi_Decoder_Internal> decoder;
    std::vector<int16_t> depunctured_symbols;
public:
    DAB_Viterbi_Decoder();
    ~DAB_Viterbi_Decoder();
    void set_traceback_length(const size_t traceback_length);
    size_t get_traceback_length() const;
    size_t get_current_decoded_bit() const;
    void reset(const size_t starting_state=0u);
    size_t update(
        tcb::span<const int8_t> punctured_symbols,
        tcb::span<const uint8_t> puncture_code,
        const size_t requested_output_symbols
    );
    uint64_t chainback(tcb::span<uint8_t> bytes_out, const size_t end_state=0u);
};