#include "viterbi_decoder.h"
#include "phil_karn_viterbi_decoder.h"

ViterbiDecoder::ViterbiDecoder(const uint8_t _poly[4], const int _input_bits) {
    vitdec = create_viterbi(_poly, _input_bits, SOFT_DECISION_VITERBI_HIGH, SOFT_DECISION_VITERBI_LOW);
}

ViterbiDecoder::~ViterbiDecoder() {
    delete_viterbi(vitdec);
}

void ViterbiDecoder::Reset() {
    init_viterbi(vitdec, 0);
}

ViterbiDecoder::DecodeResult ViterbiDecoder::Update(
    tcb::span<const viterbi_bit_t> encoded_bits,
    tcb::span<const uint8_t> puncture_code) 
{
    const int nb_encoded_bits = (int)encoded_bits.size();
    const int nb_puncture_bits = (int)puncture_code.size();

    COMPUTETYPE depunctured_bits[CODE_RATE];
    DecodeResult res;
    res.nb_decoded_bits = 0;
    res.nb_encoded_bits = 0;
    res.nb_puncture_bits = 0;

    int curr_encoded_bit = 0;
    int curr_puncture_bit = 0;

    // Punctured bits shall take the value between high and low
    // This way when the error metrics are calculated, it will be interpreted as being
    // either a 0 or 1 with equal weighting
    while (res.nb_puncture_bits < nb_encoded_bits) {
        for (int i = 0; i < CODE_RATE; i++) {
            const uint8_t v = puncture_code[curr_puncture_bit % nb_puncture_bits];
            curr_puncture_bit++;
            if (v && (curr_puncture_bit == nb_encoded_bits)) {
                return res;
            }
            depunctured_bits[i] = v ? encoded_bits[curr_encoded_bit++] : SOFT_DECISION_VITERBI_PUNCTURED;
        }
        res.nb_encoded_bits = curr_encoded_bit;
        res.nb_puncture_bits = curr_puncture_bit;
        res.nb_decoded_bits++;

        // NOTE: Phil Karn's viterbi decoder api takes the number of decoded bits we want
        //       Therefore for every 4 depunctured bits, we want 1 decoded bit
        // update_viterbi_blk_GENERIC(vitdec, depunctured_bits, 1);
        update_viterbi_blk_sse2(vitdec, depunctured_bits, 1);
    }

    return res;
}

void ViterbiDecoder::GetTraceback(tcb::span<uint8_t> out_bytes) {
    const int nb_decoded_bits = (int)(out_bytes.size() * 8);
    chainback_viterbi(vitdec, out_bytes.data(), nb_decoded_bits, 0);
}

int16_t ViterbiDecoder::GetPathError(const int state) {
    return get_error_viterbi(vitdec, state);
}