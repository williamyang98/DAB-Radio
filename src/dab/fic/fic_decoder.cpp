#include "fic_decoder.h"

#include "../algorithms/dab_viterbi_decoder.h"
#include "../algorithms/additive_scrambler.h"
#include "../algorithms/crc.h"
#include "../constants/puncture_codes.h"
#include <fmt/core.h>

#include "../dab_logging.h"
#define TAG "fic-decoder"
static auto _logger = DAB_LOG_REGISTER(TAG);
#define LOG_MESSAGE(...) DAB_LOG_MESSAGE(TAG, fmt::format(__VA_ARGS__))
#define LOG_ERROR(...) DAB_LOG_ERROR(TAG, fmt::format(__VA_ARGS__))

static const auto Generate_CRC_Calc() {
    // DOC: ETSI EN 300 401
    // Clause 5.2.1 - Fast Information Block (FIB)
    // CRC16 Polynomial is given by:
    // G(x) = x^16 + x^12 + x^5 + 1
    // POLY = 0b 0001 0000 0010 0001 = 0x1021
    static const uint16_t crc16_poly = 0x1021;
    static auto crc16_calc = new CRC_Calculator<uint16_t>(crc16_poly);
    crc16_calc->SetInitialValue(0xFFFF);    // initial value all 1s
    crc16_calc->SetFinalXORValue(0xFFFF);   // transmitted crc is 1s complemented

    return crc16_calc;
};

static auto CRC16_CALC = Generate_CRC_Calc();

FIC_Decoder::FIC_Decoder(const int _nb_encoded_bits, const int _nb_fibs_per_group)
// NOTE: 1/3 coding rate after puncturing and 1/4 code
// For all transmission modes these parameters are constant
: nb_encoded_bits(_nb_encoded_bits),
  nb_decoded_bits(_nb_encoded_bits/3),
  nb_decoded_bytes(_nb_encoded_bits/(8*3)),
  nb_fibs_per_group(_nb_fibs_per_group)
{
    vitdec = std::make_unique<DAB_Viterbi_Decoder>();
    vitdec->set_traceback_length(nb_decoded_bits);
    decoded_bytes.resize(nb_decoded_bytes);
    scrambler = std::make_unique<AdditiveScrambler>();
    scrambler->SetSyncword(0xFFFF);
}

FIC_Decoder::~FIC_Decoder() = default;

// Each group contains 3 fibs (fast information blocks) in mode I
void FIC_Decoder::DecodeFIBGroup(tcb::span<const viterbi_bit_t> encoded_bits, const int cif_index) {
    // viterbi decoding
    int curr_encoded_bit = 0;
    int curr_puncture_bit = 0;
    int curr_decoded_bit = 0;

    // DOC: ETSI EN 300 401
    // Clause 11.2 - Coding in the fast information channel
    // PI_16, PI_15 and PI_X are used
    auto PI_16 = GetPunctureCode(16);
    auto PI_15 = GetPunctureCode(15);

    // We only have the puncture codes used for transmission mode I
    // NOTE: The number of decoded bits for mode I is the same as mode II and mode IV
    //       Perhaps these other modes also use the same puncture codes??? 
    //       Refer to DOC: docs/DAB_parameters.pdf, Clause A1.1: System parameters
    //       for the number of bits per fib group for each transmission mode
    const int nb_tail_bits = 6;
    const int nb_decoded_bits_mode_I = (128*21 + 128*3 + 24)/int(DAB_Viterbi_Decoder::code_rate) - nb_tail_bits;
    if (nb_decoded_bits != nb_decoded_bits_mode_I) {
        LOG_ERROR("Expected {} encoded bits but got {}", nb_decoded_bits_mode_I, nb_decoded_bits);
        LOG_ERROR("ETSI EN 300 401 standard only gives the puncture codes used in transmission mode I");
        return;
    }

    vitdec->reset();
    {
        size_t N;
        auto encoded_bits_buf = encoded_bits;
        N = vitdec->update(encoded_bits_buf, PI_16, 128*21);
        encoded_bits_buf = encoded_bits_buf.subspan(N);
        N = vitdec->update(encoded_bits_buf, PI_15, 128*3);
        encoded_bits_buf = encoded_bits_buf.subspan(N);
        N = vitdec->update(encoded_bits_buf, PI_X, 24);
        encoded_bits_buf = encoded_bits_buf.subspan(N);
        assert(encoded_bits_buf.size() == 0);
    }

    const uint64_t error = vitdec->chainback(decoded_bytes);
    LOG_MESSAGE("error:    {}", error);

    // descrambler
    scrambler->Reset();
    for (int i = 0; i < nb_decoded_bytes; i++) {
        uint8_t b = scrambler->Process();
        decoded_bytes[i] ^= b;
    }

    // crc16 check
    const int nb_fib_bytes = nb_decoded_bytes/nb_fibs_per_group;
    const int nb_crc16_bytes = 2;
    const int nb_data_bytes = nb_fib_bytes-nb_crc16_bytes;

    for (int i = 0; i < nb_fibs_per_group; i++) {
        auto fib_buf = tcb::span(decoded_bytes).subspan(i*nb_fib_bytes, nb_fib_bytes);
        auto data_buf = fib_buf.first(nb_data_bytes);
        auto crc_buf = fib_buf.last(nb_crc16_bytes);

        const uint16_t crc16_rx = (crc_buf[0] << 8) | crc_buf[1];
        const uint16_t crc16_pred = CRC16_CALC->Process(data_buf);
        const bool is_valid = crc16_rx == crc16_pred;
        LOG_MESSAGE("[crc16] fib={}/{} is_match={} pred={:04X} got={:04X}", 
            i, nb_fibs_per_group, is_valid, crc16_pred, crc16_rx);
        
        if (is_valid) {
            obs_on_fib.Notify(data_buf);
        }
    }
}
