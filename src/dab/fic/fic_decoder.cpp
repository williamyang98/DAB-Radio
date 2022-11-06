#include "fic_decoder.h"

#include "../algorithms/viterbi_decoder.h"
#include "../algorithms/additive_scrambler.h"
#include "../algorithms/crc.h"
#include "../constants/puncture_codes.h"

#include "easylogging++.h"
#include "fmt/core.h"

#define LOG_MESSAGE(...) CLOG(INFO, "fic-decoder") << fmt::format(__VA_ARGS__)
#define LOG_ERROR(...) CLOG(ERROR, "fic-decoder") << fmt::format(__VA_ARGS__)

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
    {
        // DOC: ETSI EN 300 401
        // Clause 11.1 - Convolutional code
        // Clause 11.1.1 - Mother code
        // Octal form | Binary form | Reversed binary | Decimal form |
        //     133    | 001 011 011 |    110 110 1    |      109     |
        //     171    | 001 111 001 |    100 111 1    |       79     |
        //     145    | 001 100 101 |    101 001 1    |       83     |
        //     133    | 001 011 011 |    110 110 1    |      109     |
        const uint8_t POLYS[4] = { 109, 79, 83, 109 };
        vitdec = new ViterbiDecoder(POLYS, nb_encoded_bits);
    }

    scrambler = new AdditiveScrambler();
    scrambler->SetSyncword(0xFFFF);

    decoded_bytes = new uint8_t[nb_decoded_bytes];
}

FIC_Decoder::~FIC_Decoder() {
    delete vitdec;
    delete scrambler;

    delete [] decoded_bytes;
}

// Helper macro to run viterbi decoder with parameters
#define VITDEC_RUN(L, PI, PI_len)\
{\
    res = vitdec->Update(\
        &encoded_bits[curr_encoded_bit], L,\
        PI, PI_len);\
    curr_encoded_bit += res.nb_encoded_bits;\
    curr_puncture_bit += res.nb_puncture_bits;\
    curr_decoded_bit += res.nb_decoded_bits;\
}

// Each group contains 3 fibs (fast information blocks) in mode I
void FIC_Decoder::DecodeFIBGroup(const viterbi_bit_t* encoded_bits, const int cif_index) {
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
    const int nb_decoded_bits_mode_I = (128*21 + 128*3 + 24)/4 - 6;
    if (nb_decoded_bits != nb_decoded_bits_mode_I) {
        LOG_ERROR("Expected {} encoded bits but got {}", nb_decoded_bits_mode_I, nb_decoded_bits);
        LOG_ERROR("ETSI EN 300 401 standard only gives the puncture codes used in transmission mode I");
        return;
    }

    ViterbiDecoder::DecodeResult res;
    vitdec->Reset();
    VITDEC_RUN(128*21, PI_16, 32);
    VITDEC_RUN(128*3,  PI_15, 32);
    VITDEC_RUN(24,     PI_X,  24);

    const auto error = vitdec->GetPathError();
    vitdec->GetTraceback(decoded_bytes, nb_decoded_bits);

    LOG_MESSAGE("encoded:  {}/{}", curr_encoded_bit, nb_encoded_bits);
    LOG_MESSAGE("decoded:  {}/{}", curr_decoded_bit, nb_decoded_bits);
    LOG_MESSAGE("puncture: {}", curr_puncture_bit);
    LOG_MESSAGE("error:    {}", error);

    // descrambler
    scrambler->Reset();
    for (int i = 0; i < nb_decoded_bytes; i++) {
        uint8_t b = scrambler->Process();
        decoded_bytes[i] ^= b;
    }

    // crc16 check
    const int nb_fib_bytes = nb_decoded_bytes/nb_fibs_per_group;
    const int nb_data_bytes = nb_fib_bytes-2;
    for (int i = 0; i < nb_fibs_per_group; i++) {
        auto* fib_buf = &decoded_bytes[i*nb_fib_bytes];

        uint16_t crc16_rx = 0u;
        crc16_rx |= static_cast<uint16_t>(fib_buf[nb_data_bytes]) << 8;
        crc16_rx |= fib_buf[nb_data_bytes+1];

        const uint16_t crc16_pred = CRC16_CALC->Process(fib_buf, nb_data_bytes);
        const bool is_valid = crc16_rx == crc16_pred;
        LOG_MESSAGE("[crc16] fib={}/{} is_match={} pred={:04X} got={:04X}", 
            i, nb_fibs_per_group, is_valid, crc16_pred, crc16_rx);
        
        if (is_valid) {
            obs_on_fib.Notify(fib_buf, nb_fib_bytes);
        }
    }
}
