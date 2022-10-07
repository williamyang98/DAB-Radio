#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <io.h>
#include <fcntl.h>

#include "./getopt/getopt.h"

#include "viterbi_decoder.h"
#include "CRC.h"

#define PRINT_LOG 1

#if PRINT_LOG 
  #define LOG_MESSAGE(...) fprintf(stderr, ##__VA_ARGS__)
#else
  #define LOG_MESSAGE(...) (void)0
#endif

void ProcessFrame(const uint8_t* buf, ViterbiDecoder* vitdec);
void ProcessFICGroup(const uint8_t* buf, ViterbiDecoder* vitdec);

void usage() {
    fprintf(stderr, 
        "process_frames, decoded DAB frame data\n\n"
        "\t[-i input filename (default: None)]\n"
        "\t    If no file is provided then stdin is used\n"
        "\t[-h (show usage)]\n"
    );
}

class Scrambler 
{
private:
    uint16_t syncword;
    uint16_t reg;
public:
    uint8_t Process() {
        uint8_t b = 0x00;
        for (int i = 0; i < 8; i++) {
            uint8_t v = 0;
            // 1 + x^-5 + x^-9
            v ^= ((reg >> 8) & 0b1);
            v ^= ((reg >> 4) & 0b1);
            b |= (v << i);
            reg = (reg << 1) | v;
        }
        return b;
    }

    void SetSyncword(const uint16_t _syncword) {
        syncword = _syncword;
    }
    void Reset() {
        reg = syncword;
    }
};

int main(int argc, char** argv)
{
    char* rd_filename = NULL;

    int opt; 
    while ((opt = getopt(argc, argv, "i:h")) != -1) {
        switch (opt) {
        case 'i':
            rd_filename = optarg;
            break;
        case 'h':
        default:
            usage();
            return 0;
        }
    }

    // app startup
    FILE* fp_in = stdin;
    if (rd_filename != NULL) {
        errno_t err = fopen_s(&fp_in, rd_filename, "r");
        if (err != 0) {
            LOG_MESSAGE("Failed to open file for reading\n");
            return 1;
        }
    }

    _setmode(_fileno(fp_in), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    const int N = 28800;
    auto buf = new uint8_t[N];

    const int L = 4;
    const int K = 7;

    // const uint8_t CONV_CODES[L] = {133,171,145,133};
    // const uint8_t CONV_CODES[L] = {155, 117, 123, 155};
    // NOTE: above is in octal form
    // 155 = 1 5 5 = 001 101 101 = 0110 1101
    // const uint8_t CONV_CODES[L] = {0b01011011, 0b01111001, 0b01100101, 0b01011011};
    const uint8_t CONV_CODES[L] = {0b1101101, 0b1001111, 0b1010011, 0b1101101};

    auto trellis = Trellis(CONV_CODES, L, K);
    auto vitdec = ViterbiDecoder(&trellis, 96);

    while (true) {
        const auto nb_read = fread(buf, sizeof(uint8_t), N, fp_in);
        if (nb_read != N) {
            fprintf(stderr, "Failed to read %d bytes\n", N);
            return 1;
        }
        ProcessFrame(buf, &vitdec);
    }

    return 0;
}

void ProcessFrame(const uint8_t* buf, ViterbiDecoder* vitdec)
{
    const int nb_frame_length = 28800;
    const int nb_symbols = 75;
    const int nb_sym_length = nb_frame_length / nb_symbols;

    const int nb_fic_symbols = 3;

    const auto* fic_buf = &buf[0];
    const auto* msc_buf = &buf[nb_fic_symbols*nb_sym_length];

    const int nb_fic_length = nb_sym_length*nb_fic_symbols;
    const int nb_msc_length = nb_sym_length*(nb_symbols-nb_fic_symbols);

    const int nb_fic_groups = 4;
    const int nb_fic_group_length = nb_fic_length / nb_fic_groups;
    for (int i = 0; i < nb_fic_groups; i++) {
        const auto* fic_group_buf = &fic_buf[i*nb_fic_group_length];
        ProcessFICGroup(fic_group_buf, vitdec);
    }
}

void ProcessFICGroup(const uint8_t* encoded_bytes, ViterbiDecoder* vitdec)
{
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

    const uint8_t PI_15[32] = { 1,1,1,0, 1,1,1,0, 1,1,1,0, 1,1,1,0, 1,1,1,0, 1,1,1,0, 1,1,1,0, 1,1,0,0 };
    const uint8_t PI_16[32] = { 1,1,1,0, 1,1,1,0, 1,1,1,0, 1,1,1,0, 1,1,1,0, 1,1,1,0, 1,1,1,0, 1,1,1,0 };
    const uint8_t PI_X[24]  = { 1,1,0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0,0,1,1,0,0 };

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

    LOG_MESSAGE("encoded:  %d/%d\n", curr_encoded_bit, nb_encoded_bits);
    LOG_MESSAGE("decoded:  %d/%d\n", curr_decoded_bit, nb_decoded_bits);
    LOG_MESSAGE("puncture: %d\n", curr_puncture_bit);
    LOG_MESSAGE("error:    %d\n", error);

    // pack into bytes for further processing
    for (int i = 0; i < nb_decoded_bytes; i++) {
        auto& b = decoded_bytes[i];
        b = 0x00;
        for (int j = 0; j < 8; j++) {
            b |= (decoded_bits[8*i + j] << j);
        }
    }

    Scrambler scrambler;
    scrambler.SetSyncword(0xFFFF);
    scrambler.Reset();

    // descrambler
    for (int i = 0; i < nb_decoded_bytes; i++) {
        uint8_t b = scrambler.Process();
        decoded_bytes[i] ^= b;
    }

    const auto print_buf = [](const uint8_t* buf, const int N) {
        for (int i = 0; i < N; i++) {
            for (int j = 0; j < 8; j++) {
                printf("%d,", (buf[i] >> j) & 0b1);
            }
        }
    };

    // crc16 check
    const int nb_figs = 3;
    const int nb_fig_bytes = nb_decoded_bytes/nb_figs;
    const int nb_data_bytes = nb_fig_bytes-2;
    for (int i = 0; i < 3; i++) {
        auto* fig_buf = &decoded_bytes[i*nb_fig_bytes];

        uint16_t crc16_rx = 0u;
        crc16_rx |= static_cast<uint16_t>(fig_buf[nb_data_bytes+1]) << 8;
        crc16_rx |= fig_buf[nb_data_bytes];
        // crc16 is inverted
        crc16_rx ^= 0xFFFF;

        const uint16_t CRC_POLY = 0x1021;
        const CRC::Parameters<crcpp_uint16, 16> parameters = { 
            CRC_POLY, 0xFFFF, 0x0000, true, true };
        const uint16_t crc16_pred = CRC::Calculate(fig_buf, nb_data_bytes, parameters);
        LOG_MESSAGE("%04x/%04x (%d)\n", crc16_rx, crc16_pred, crc16_rx == crc16_pred);
    }
}
