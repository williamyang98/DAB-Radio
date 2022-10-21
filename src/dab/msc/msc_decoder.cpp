#include "msc_decoder.h"

#include "cif_deinterleaver.h"
#include "database/dab_database_entities.h"
#include "algorithms/viterbi_decoder.h"
#include "algorithms/additive_scrambler.h"
#include "constants/puncture_codes.h"
#include "constants/subchannel_protection_tables.h"

#include "easylogging++.h"
#include "fmt/core.h"

#define LOG_MESSAGE(...) CLOG(INFO, "msc-decoder") << fmt::format(##__VA_ARGS__)
#define LOG_ERROR(...) CLOG(ERROR, "msc-decoder") << fmt::format(##__VA_ARGS__)

// NOTE: Capacity channel sizes for mode I are constant
constexpr int NB_CU_BITS = 64;
constexpr int NB_CU_BYTES = NB_CU_BITS/8;

MSC_Decoder::MSC_Decoder(const Subchannel _subchannel) 
: subchannel(_subchannel), 
  nb_encoded_bits(subchannel.length*NB_CU_BITS),
  nb_encoded_bytes(subchannel.length*NB_CU_BYTES)
{
    encoded_bits_buf = new uint8_t[nb_encoded_bits];
    decoded_bits_buf = new uint8_t[nb_encoded_bits];
    decoded_bytes_buf = new uint8_t[nb_encoded_bytes];

    deinterleaver = new CIF_Deinterleaver(nb_encoded_bytes);

    // OCTAL FORM  {133,171,145,133};
    // BINARY FORM {0b01011011, 0b01111001, 0b01100101, 0b01011011};
    // We reverse the bit order within a byte to match transmission
    const int L = 4;
    const int K = 7;
    const int traceback_length = 15;
    const uint8_t CONV_CODES[L] = {0b1101101, 0b1001111, 0b1010011, 0b1101101};
    trellis = new Trellis(CONV_CODES, L, K);
    vitdec = new ViterbiDecoder(trellis, traceback_length);
    scrambler = new AdditiveScrambler();
    scrambler->SetSyncword(0xFFFF);
}

MSC_Decoder::~MSC_Decoder() {
    delete [] encoded_bits_buf;
    delete [] decoded_bits_buf;
    delete [] decoded_bytes_buf;
    delete deinterleaver;
    delete trellis;
    delete vitdec;
    delete scrambler;
}

int MSC_Decoder::DecodeCIF(const uint8_t* buf, const int N) {
    const int start_byte = subchannel.start_address*NB_CU_BYTES;
    const auto* subchannel_buf = &buf[start_byte];
    deinterleaver->Consume(subchannel_buf);

    // Deinterleaver doesn't have enough frames
    if (!deinterleaver->Deinterleave(encoded_bits_buf)) {
        return 0;
    }

    // viterbi decoding
    if (!subchannel.is_uep) {
        LOG_MESSAGE("Decoding EEP");
        return DecodeEEP();
    } else {
        LOG_MESSAGE("Decoding UEP");
        return DecodeUEP();
    }
}

// Helper macro to run viterbi decoder with parameters
#define VITDEC_RUN(L, PI, PI_len, flush)\
{\
    res = vitdec->Decode(\
        &encoded_bits_buf[curr_encoded_bit], nb_encoded_bits-curr_encoded_bit,\
        PI, PI_len,\
        &decoded_bits_buf[curr_decoded_bit], nb_encoded_bits-curr_decoded_bit,\
        L, flush);\
    curr_encoded_bit += res.nb_encoded_bits;\
    curr_puncture_bit += res.nb_puncture_bits;\
    curr_decoded_bit += res.nb_decoded_bits;\
}

int MSC_Decoder::DecodeEEP() {
    EEP_Descriptor descriptor;
    if (subchannel.eep_type == EEP_Type::TYPE_A) {
        if (subchannel.length == 8) {
            descriptor = EEP_PROT_2A_SPECIAL;
        } else {
            descriptor = EEP_PROTECTION_TABLE_TYPE_A[subchannel.eep_prot_level];
        }
    } else {
        descriptor = EEP_PROTECTION_TABLE_TYPE_B[subchannel.eep_prot_level];
    }

    const int n = subchannel.length / descriptor.capacity_unit_multiple;
    int curr_encoded_bit = 0;
    int curr_puncture_bit = 0;
    int curr_decoded_bit = 0;
    ViterbiDecoder::DecodeResult res;

    vitdec->Reset();
    const int TOTAL_PUNCTURE_CODES = 2;
    for (int i = 0; i < TOTAL_PUNCTURE_CODES; i++) {
        const int Lx = descriptor.Lx[i].GetLx(n);
        const auto puncture_code = GetPunctureCode(descriptor.PIx[i]);
        VITDEC_RUN(128*Lx, puncture_code, 32, false);
    }
    VITDEC_RUN(24, PI_X, 24, true);

    const uint32_t error = vitdec->GetPathError();
    LOG_MESSAGE("encoded:  {}/{}", curr_encoded_bit, nb_encoded_bits);
    LOG_MESSAGE("decoded:  {}/{}", curr_decoded_bit, nb_encoded_bits);
    LOG_MESSAGE("puncture: {}", curr_puncture_bit);
    LOG_MESSAGE("error:    {}", error);

    // NOTE: we are placing the bits in reversed order for correct bit order
    // Remove tail bits
    const int nb_tail_bits = 24/4;
    const int nb_decoded_bits = curr_decoded_bit-nb_tail_bits;
    const int nb_decoded_bytes = nb_decoded_bits/8;

    // converts bits to bytes
    for (int i = 0; i < nb_decoded_bytes; i++) {
        auto& b = decoded_bytes_buf[i];
        b = 0x00;
        for (int j = 0; j < 8; j++) {
            b |= (decoded_bits_buf[8*i + j] << (7-j));
        }
    }

    // descrambler
    scrambler->Reset();
    for (int i = 0; i < nb_decoded_bytes; i++) {
        uint8_t b = scrambler->Process();
        decoded_bytes_buf[i] ^= b;
    }

    return nb_decoded_bytes;
}

// TODO: We don't have any samples to test if UEP decoding works
int MSC_Decoder::DecodeUEP() {
    const auto descriptor = UEP_PROTECTION_TABLE[subchannel.uep_prot_index]; 

    int curr_encoded_bit = 0;
    int curr_puncture_bit = 0;
    int curr_decoded_bit = 0;
    ViterbiDecoder::DecodeResult res;

    vitdec->Reset();
    const int TOTAL_PUNCTURE_CODES = 4;
    for (int i = 0; i < TOTAL_PUNCTURE_CODES; i++) {
        const int Lx = descriptor.Lx[i];
        const auto puncture_code = GetPunctureCode(descriptor.PIx[i]);
        VITDEC_RUN(128*Lx, puncture_code, 32, false);
    }
    VITDEC_RUN(24, PI_X, 24, true);

    const uint32_t error = vitdec->GetPathError();
    LOG_MESSAGE("encoded:  {}/{}", curr_encoded_bit, nb_encoded_bits);
    LOG_MESSAGE("decoded:  {}/{}", curr_decoded_bit, nb_encoded_bits);
    LOG_MESSAGE("puncture: {}", curr_puncture_bit);
    LOG_MESSAGE("error:    {}", error);

    // NOTE: we are placing the bits in reversed order for correct bit order
    // Remove tail bits
    // TODO: How to we deal with padding bits?
    const int nb_tail_bits = 24/4;
    const int nb_decoded_bits = curr_decoded_bit-nb_tail_bits;
    const int nb_decoded_bytes = nb_decoded_bits/8;

    // converts bits to bytes
    for (int i = 0; i < nb_decoded_bytes; i++) {
        auto& b = decoded_bytes_buf[i];
        b = 0x00;
        for (int j = 0; j < 8; j++) {
            b |= (decoded_bits_buf[8*i + j] << (7-j));
        }
    }

    // descrambler
    scrambler->Reset();
    for (int i = 0; i < nb_decoded_bytes; i++) {
        uint8_t b = scrambler->Process();
        decoded_bytes_buf[i] ^= b;
    }

    return nb_decoded_bytes;
}