#include "fic_decoder.h"

#include "../algorithms/viterbi_decoder.h"
#include "../algorithms/additive_scrambler.h"
#include "../algorithms/crc.h"
#include "../constants/puncture_codes.h"

#include "easylogging++.h"
#include "fmt/core.h"

#define LOG_MESSAGE(...) CLOG(INFO, "fic-decoder") << fmt::format(##__VA_ARGS__)
#define LOG_ERROR(...) CLOG(ERROR, "fic-decoder") << fmt::format(##__VA_ARGS__)

FIC_Decoder::FIC_Decoder()
// NOTE: 1/3 coding rate after puncturing and 1/4 code
// For mode I transmission these parameters are constant
: nb_encoded_bytes(288),
  nb_encoded_bits(288*8),
  nb_decoded_bits(288*8/3),
  nb_decoded_bytes(288/3)
{
    {
        // DOC: ETSI EN 300 401
        // Clause 5.2.1 - Fast Information Block (FIB)
        // CRC16 Polynomial is given by:
        // G(x) = x^16 + x^12 + x^5 + 1
        // POLY = 0b 0001 0000 0010 0001 = 0x1021
        const uint16_t crc16_poly = 0x1021;
        crc16_calc = new CRC_Calculator<uint16_t>(crc16_poly);
        crc16_calc->SetInitialValue(0xFFFF);    // initial value all 1s
        crc16_calc->SetFinalXORValue(0xFFFF);   // transmitted crc is 1s complemented
    }


    const uint8_t POLYS[4] = { 109, 79, 83, 109 };
    vitdec = new ViterbiDecoder(POLYS, nb_encoded_bits);
    scrambler = new AdditiveScrambler();
    scrambler->SetSyncword(0xFFFF);

    decoded_bytes = new uint8_t[nb_decoded_bytes];
}

FIC_Decoder::~FIC_Decoder() {
    // delete crc16_table;
    delete crc16_calc;
    delete vitdec;
    delete scrambler;

    delete [] decoded_bytes;
}

// Each group contains 3 fibs (fast information blocks)
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

    ViterbiDecoder::DecodeResult res;

    vitdec->Reset();
    res = vitdec->Update(
        &encoded_bits[curr_encoded_bit], 128*21,
        PI_16, 32);
    curr_encoded_bit += res.nb_encoded_bits;
    curr_puncture_bit += res.nb_puncture_bits;
    curr_decoded_bit += res.nb_decoded_bits;

    res = vitdec->Update(
        &encoded_bits[curr_encoded_bit], 128*3,
        PI_15, 32);
    curr_encoded_bit += res.nb_encoded_bits;
    curr_puncture_bit += res.nb_puncture_bits;
    curr_decoded_bit += res.nb_decoded_bits;

    res = vitdec->Update(
        &encoded_bits[curr_encoded_bit], 24,
        PI_X, 24);
    curr_encoded_bit += res.nb_encoded_bits;
    curr_puncture_bit += res.nb_puncture_bits;
    curr_decoded_bit += res.nb_decoded_bits;

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
    const int nb_fibs = 3;
    const int nb_fib_bytes = nb_decoded_bytes/nb_fibs;
    const int nb_data_bytes = nb_fib_bytes-2;
    for (int i = 0; i < nb_fibs; i++) {
        auto* fib_buf = &decoded_bytes[i*nb_fib_bytes];

        uint16_t crc16_rx = 0u;
        crc16_rx |= static_cast<uint16_t>(fib_buf[nb_data_bytes]) << 8;
        crc16_rx |= fib_buf[nb_data_bytes+1];

        const uint16_t crc16_pred = crc16_calc->Process(fib_buf, nb_data_bytes);
        const bool is_valid = crc16_rx == crc16_pred;
        LOG_MESSAGE("[crc16] fib={} is_match={} pred={:04X} got={:04X}", 
            i, is_valid, crc16_pred, crc16_rx);
        
        if (is_valid) {
            obs_on_fib.Notify(fib_buf, nb_fib_bytes);
        }
    }
}
