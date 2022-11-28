#include "msc_decoder.h"

#include "cif_deinterleaver.h"
#include "database/dab_database_entities.h"
#include "algorithms/viterbi_decoder.h"
#include "algorithms/additive_scrambler.h"
#include "constants/puncture_codes.h"
#include "constants/subchannel_protection_tables.h"

#include "easylogging++.h"
#include "fmt/core.h"

#define LOG_MESSAGE(...) CLOG(INFO, "msc-decoder") << fmt::format(__VA_ARGS__)
#define LOG_ERROR(...) CLOG(ERROR, "msc-decoder") << fmt::format(__VA_ARGS__)

// NOTE: Capacity channel sizes for mode I are constant
constexpr int NB_CU_BITS = 64;
constexpr int NB_CU_BYTES = NB_CU_BITS/8;

MSC_Decoder::MSC_Decoder(const Subchannel _subchannel) 
: subchannel(_subchannel), 
  nb_encoded_bits(subchannel.length*NB_CU_BITS),
  nb_encoded_bytes(subchannel.length*NB_CU_BYTES)
{
    encoded_bits_buf.resize(nb_encoded_bits);
    decoded_bytes_buf.resize(nb_encoded_bytes);

    deinterleaver = std::make_unique<CIF_Deinterleaver>(nb_encoded_bytes);

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
        vitdec = std::make_unique<ViterbiDecoder>(POLYS, nb_encoded_bits);
    }

    scrambler = std::make_unique<AdditiveScrambler>();
    scrambler->SetSyncword(0xFFFF);
}

MSC_Decoder::~MSC_Decoder() = default;

tcb::span<uint8_t> MSC_Decoder::DecodeCIF(tcb::span<const viterbi_bit_t> buf) {
    const int N = (int)buf.size();
    const int start_bit = subchannel.start_address*NB_CU_BITS;
    const int end_bit = start_bit + nb_encoded_bits;
    if (end_bit > N) {
        LOG_ERROR("Subchannel bits {}:{} overflows MSC channel with {} bits", 
            start_bit, end_bit, N);
        return {};
    }

    const int total_bits = end_bit-start_bit;
    auto subchannel_buf = buf.subspan(start_bit, total_bits);
    deinterleaver->Consume(subchannel_buf);

    // Deinterleaver doesn't have enough frames
    if (!deinterleaver->Deinterleave(encoded_bits_buf)) {
        return {};
    }

    // viterbi decoding
    int nb_decoded_bytes = 0;
    if (!subchannel.is_uep) {
        LOG_MESSAGE("Decoding EEP");
        nb_decoded_bytes = DecodeEEP();
    } else {
        LOG_MESSAGE("Decoding UEP");
        nb_decoded_bytes = DecodeUEP();
    }
    return { decoded_bytes_buf.data(), (size_t)nb_decoded_bytes };
}

// Helper macro to run viterbi decoder with parameters
#define VITDEC_RUN(L, PI)\
{\
    res = vitdec->Update(\
        {&encoded_bits_buf[curr_encoded_bit], (size_t)L},\
        PI);\
    curr_encoded_bit += res.nb_encoded_bits;\
    curr_puncture_bit += res.nb_puncture_bits;\
    curr_decoded_bit += res.nb_decoded_bits;\
}

int MSC_Decoder::DecodeEEP() {
    const auto descriptor = GetEEPDescriptor(subchannel);

    const int n = subchannel.length / descriptor.capacity_unit_multiple;
    int curr_encoded_bit = 0;
    int curr_puncture_bit = 0;
    int curr_decoded_bit = 0;
    ViterbiDecoder::DecodeResult res;

    // DOC: ETSI EN 300 401
    // Clause 11.3.2 - Equal Error Protection (EEP) coding  
    vitdec->Reset();
    const int TOTAL_PUNCTURE_CODES = 2;
    for (int i = 0; i < TOTAL_PUNCTURE_CODES; i++) {
        const int Lx = descriptor.Lx[i].GetLx(n);
        const auto puncture_code = GetPunctureCode(descriptor.PIx[i]);
        VITDEC_RUN(128*Lx, puncture_code);
    }
    VITDEC_RUN(24, PI_X);

    const auto error = vitdec->GetPathError();

    LOG_MESSAGE("encoded:  {}/{}", curr_encoded_bit, nb_encoded_bits);
    LOG_MESSAGE("decoded:  {}/{}", curr_decoded_bit, nb_encoded_bits);
    LOG_MESSAGE("puncture: {}", curr_puncture_bit);
    LOG_MESSAGE("error:    {}", error);

    const int nb_tail_bits = 24/4;
    const int nb_decoded_bits = curr_decoded_bit-nb_tail_bits;
    const int nb_decoded_bytes = nb_decoded_bits/8;
    vitdec->GetTraceback({decoded_bytes_buf.data(), (size_t)nb_decoded_bytes});

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
    const auto descriptor = GetUEPDescriptor(subchannel);

    int curr_encoded_bit = 0;
    int curr_puncture_bit = 0;
    int curr_decoded_bit = 0;
    ViterbiDecoder::DecodeResult res;

    // DOC: ETSI EN 300 401
    // Clause 11.3.1 - Unequal Error Protection (UEP) coding 
    vitdec->Reset();
    const int TOTAL_PUNCTURE_CODES = 4;
    for (int i = 0; i < TOTAL_PUNCTURE_CODES; i++) {
        const int Lx = descriptor.Lx[i];
        const auto puncture_code = GetPunctureCode(descriptor.PIx[i]);
        VITDEC_RUN(128*Lx, puncture_code);
    }
    VITDEC_RUN(24, PI_X);

    const auto error = vitdec->GetPathError();

    LOG_MESSAGE("encoded:  {}/{}", curr_encoded_bit, nb_encoded_bits);
    LOG_MESSAGE("decoded:  {}/{}", curr_decoded_bit, nb_encoded_bits);
    LOG_MESSAGE("puncture: {}", curr_puncture_bit);
    LOG_MESSAGE("error:    {}", error);

    // TODO: How to we deal with padding bits?
    const int nb_tail_bits = 24/4;
    const int nb_decoded_bits = curr_decoded_bit-nb_tail_bits;
    const int nb_decoded_bytes = nb_decoded_bits/8;
    vitdec->GetTraceback({decoded_bytes_buf.data(), (size_t)nb_decoded_bytes});

    // descrambler
    scrambler->Reset();
    for (int i = 0; i < nb_decoded_bytes; i++) {
        uint8_t b = scrambler->Process();
        decoded_bytes_buf[i] ^= b;
    }

    return nb_decoded_bytes;
}