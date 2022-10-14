#include "fic_decoder.h"

#include "viterbi_decoder.h"
#include "additive_scrambler.h"
#include "puncture_codes.h"

FIC_Decoder::FIC_Decoder()
: crc16_poly(0x1021)
{
    const auto crc16_params = CRC::Parameters<crcpp_uint16, 16>{
        crc16_poly, 0xFFFF, 0x0000, false, false };
    crc16_table = new CRC::Table<crcpp_uint16, 16>(crc16_params);

    // const uint8_t CONV_CODES[L] = {133,171,145,133};
    // const uint8_t CONV_CODES[L] = {155, 117, 123, 155};
    // NOTE: above is in octal form
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
}

FIC_Decoder::~FIC_Decoder() {
    delete crc16_table;
    delete trellis;
    delete vitdec;
    delete scrambler;
}

void FIC_Decoder::DecodeFIBGroup(const uint8_t* encoded_bytes, const int cif_index) {
    const int nb_encoded_bytes = 288;
    const int nb_encoded_bits = nb_encoded_bytes*8;

    // unpack bits
    static uint8_t* encoded_bits = new uint8_t[nb_encoded_bits];
    for (int i = 0; i < nb_encoded_bytes; i++) {
        const uint8_t b = encoded_bytes[i];
        for (int j = 0; j < 8; j++) {
            encoded_bits[8*i + j] = (b >> j) & 0b1;
        }
    }

    // 1/3 coding rate after puncturing and 1/4 code
    const int nb_decoded_bits = nb_encoded_bits/3;
    const int nb_decoded_bytes = nb_decoded_bits/8;
    static uint8_t* decoded_bits = new uint8_t[nb_decoded_bits];
    static uint8_t* decoded_bytes = new uint8_t[nb_decoded_bytes];

    // viterbi decoding
    int curr_encoded_bit = 0;
    int curr_puncture_bit = 0;
    int curr_decoded_bit = 0;

    ViterbiDecoder::DecodeResult res;

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

    // LOG_MESSAGE("encoded:  %d/%d\n", curr_encoded_bit, nb_encoded_bits);
    // LOG_MESSAGE("decoded:  %d/%d\n", curr_decoded_bit, nb_decoded_bits);
    // LOG_MESSAGE("puncture: %d\n", curr_puncture_bit);
    // LOG_MESSAGE("error:    %d\n", error);

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
    for (int i = 0; i < 3; i++) {
        auto* fib_buf = &decoded_bytes[i*nb_fib_bytes];

        uint16_t crc16_rx = 0u;
        crc16_rx |= static_cast<uint16_t>(fib_buf[nb_data_bytes]) << 8;
        crc16_rx |= fib_buf[nb_data_bytes+1];
        // crc16 is inverted at transmission
        crc16_rx ^= 0xFFFF;

        const uint16_t crc16_pred = CRC::Calculate(fib_buf, nb_data_bytes, *crc16_table);
        const bool is_valid = crc16_rx == crc16_pred;
        // LOG_MESSAGE("%04x/%04x (%d)\n", crc16_rx, crc16_pred, is_valid);
        if (is_valid && callback) {
            // LOG_MESSAGE("[%d] FIG group start (%d)\n", cif_index, i);
            callback->OnDecodeFIBGroup(fib_buf, nb_fib_bytes, cif_index);
        }
    }

}
