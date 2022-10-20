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
        const uint16_t crc16_poly = 0x1021;
        crc16_calc = new CRC_Calculator<uint16_t>(crc16_poly);
        crc16_calc->SetInitialValue(0xFFFF);    // initial value all 1s
        crc16_calc->SetFinalXORValue(0xFFFF);   // transmitted crc is 1s complemented
    }


    // const uint8_t CONV_CODES[L] = {133,171,145,133};
    // const uint8_t CONV_CODES[L] = {155, 117, 123, 155};
    // NOTE: above is in octal form, we want it to be in binary form
    // 155 = 1 5 5 = 001 101 101 = 0110 1101
    // const uint8_t CONV_CODES[L] = {0b01011011, 0b01111001, 0b01100101, 0b01011011};
    const int L = 4;
    const int K = 7;
    const int traceback_length = 15;
    const uint8_t CONV_CODES[L] = {0b1101101, 0b1001111, 0b1010011, 0b1101101};
    trellis = new Trellis(CONV_CODES, L, K);
    vitdec = new ViterbiDecoder(trellis, traceback_length);

    scrambler = new AdditiveScrambler();
    scrambler->SetSyncword(0xFFFF);

    encoded_bits = new uint8_t[nb_encoded_bits];
    decoded_bits = new uint8_t[nb_decoded_bits];
    decoded_bytes = new uint8_t[nb_decoded_bytes];
}

FIC_Decoder::~FIC_Decoder() {
    // delete crc16_table;
    delete crc16_calc;
    delete trellis;
    delete vitdec;
    delete scrambler;

    delete [] encoded_bits;
    delete [] decoded_bits;
    delete [] decoded_bytes;
}

// Each group contains 3 fibs (fast information blocks)
void FIC_Decoder::DecodeFIBGroup(const uint8_t* encoded_bytes, const int cif_index) {
    // unpack bits
    for (int i = 0; i < nb_encoded_bytes; i++) {
        const uint8_t b = encoded_bytes[i];
        for (int j = 0; j < 8; j++) {
            encoded_bits[8*i + j] = (b >> j) & 0b1;
        }
    }

    // viterbi decoding
    int curr_encoded_bit = 0;
    int curr_puncture_bit = 0;
    int curr_decoded_bit = 0;

    ViterbiDecoder::DecodeResult res;

    auto PI_16 = GetPunctureCode(16);
    auto PI_15 = GetPunctureCode(15);

    vitdec->Reset();
    res = vitdec->Decode(
        &encoded_bits[curr_encoded_bit], nb_encoded_bits-curr_encoded_bit, 
        PI_16, 32, 
        &decoded_bits[curr_decoded_bit], nb_decoded_bits-curr_decoded_bit,
        128*21, false);
    curr_encoded_bit += res.nb_encoded_bits;
    curr_puncture_bit += res.nb_puncture_bits;
    curr_decoded_bit += res.nb_decoded_bits;

    res = vitdec->Decode(
        &encoded_bits[curr_encoded_bit], nb_encoded_bits-curr_encoded_bit, 
        PI_15, 32, 
        &decoded_bits[curr_decoded_bit], nb_decoded_bits-curr_decoded_bit,
        128*3, false);
    curr_encoded_bit += res.nb_encoded_bits;
    curr_puncture_bit += res.nb_puncture_bits;
    curr_decoded_bit += res.nb_decoded_bits;

    res = vitdec->Decode(
        &encoded_bits[curr_encoded_bit], nb_encoded_bits-curr_encoded_bit, 
        PI_X, 24, 
        &decoded_bits[curr_decoded_bit], nb_decoded_bits-curr_decoded_bit,
        24, true);
    curr_encoded_bit += res.nb_encoded_bits;
    curr_puncture_bit += res.nb_puncture_bits;
    curr_decoded_bit += res.nb_decoded_bits;

    const uint32_t error = vitdec->GetPathError();

    LOG_MESSAGE("encoded:  {}/{}", curr_encoded_bit, nb_encoded_bits);
    LOG_MESSAGE("decoded:  {}/{}", curr_decoded_bit, nb_decoded_bits);
    LOG_MESSAGE("puncture: {}", curr_puncture_bit);
    LOG_MESSAGE("error:    {}", error);

    // pack into bytes for further processing
    // NOTE: we are placing the bits in reversed order for correct bit order
    for (int i = 0; i < nb_decoded_bytes; i++) {
        auto& b = decoded_bytes[i];
        b = 0x00;
        for (int j = 0; j < 8; j++) {
            b |= (decoded_bits[8*i + j] << (7-j));
        }
    }

    scrambler->Reset();

    // descrambler
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
        if (is_valid && callback) {
            callback->OnDecodeFIBGroup(fib_buf, nb_fib_bytes, cif_index);
        }    
    }
}
