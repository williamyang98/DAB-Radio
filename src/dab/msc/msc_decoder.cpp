#include "./msc_decoder.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <memory>
#include <fmt/format.h>
#include "utility/span.h"
#include "viterbi_config.h"
#include "./cif_deinterleaver.h"
#include "../algorithms/additive_scrambler.h"
#include "../algorithms/dab_viterbi_decoder.h"
#include "../constants/puncture_codes.h"
#include "../constants/subchannel_protection_tables.h"
#include "../dab_logging.h"
#include "../database/dab_database_entities.h"
#define TAG "msc-decoder"
static auto _logger = DAB_LOG_REGISTER(TAG);
#define LOG_MESSAGE(...) DAB_LOG_MESSAGE(TAG, fmt::format(__VA_ARGS__))
#define LOG_ERROR(...) DAB_LOG_ERROR(TAG, fmt::format(__VA_ARGS__))

// NOTE: Capacity channel sizes for mode I are constant
constexpr int TOTAL_CAPACITY_UNIT_BITS = 64;
constexpr int TOTAL_CAPACITY_UNIT_BYTES = TOTAL_CAPACITY_UNIT_BITS/8;

MSC_Decoder::MSC_Decoder(const Subchannel subchannel) 
: m_subchannel(subchannel), 
  m_nb_encoded_bits(m_subchannel.length*TOTAL_CAPACITY_UNIT_BITS),
  m_nb_encoded_bytes(m_subchannel.length*TOTAL_CAPACITY_UNIT_BYTES)
{
    m_encoded_bits_buf.resize(m_nb_encoded_bits);
    m_decoded_bytes_buf.resize(m_nb_encoded_bytes);

    m_deinterleaver = std::make_unique<CIF_Deinterleaver>(m_nb_encoded_bytes);

    m_vitdec = std::make_unique<DAB_Viterbi_Decoder>();
    // NOTE: The number of encoded symbols is always greater than the number of input bits
    // TODO: Can we set this to a more conservative number to save memory?
    m_vitdec->set_traceback_length(m_nb_encoded_bits);

    m_scrambler = std::make_unique<AdditiveScrambler>();
    m_scrambler->SetSyncword(0xFFFF);
}

MSC_Decoder::~MSC_Decoder() = default;

tcb::span<uint8_t> MSC_Decoder::DecodeCIF(tcb::span<const viterbi_bit_t> buf) {
    const int N = (int)buf.size();
    const int start_bit = m_subchannel.start_address*TOTAL_CAPACITY_UNIT_BITS;
    const int end_bit = start_bit + m_nb_encoded_bits;
    if (end_bit > N) {
        LOG_ERROR("Subchannel bits {}:{} overflows MSC channel with {} bits", 
            start_bit, end_bit, N);
        return {};
    }

    const int total_bits = end_bit-start_bit;
    auto subchannel_buf = buf.subspan(start_bit, total_bits);
    m_deinterleaver->Consume(subchannel_buf);

    // Deinterleaver doesn't have enough frames
    if (!m_deinterleaver->Deinterleave(m_encoded_bits_buf)) {
        return {};
    }

    // viterbi decoding
    int nb_decoded_bytes = 0;
    if (!m_subchannel.is_uep) {
        LOG_MESSAGE("Decoding EEP");
        nb_decoded_bytes = DecodeEEP();
    } else {
        LOG_MESSAGE("Decoding UEP");
        nb_decoded_bytes = DecodeUEP();
    }
    return { m_decoded_bytes_buf.data(), size_t(nb_decoded_bytes) };
}

int MSC_Decoder::DecodeEEP() {
    const auto descriptor = GetEEPDescriptor(m_subchannel);

    const int n = m_subchannel.length / descriptor.capacity_unit_multiple;

    // DOC: ETSI EN 300 401
    // Clause 11.3.2 - Equal Error Protection (EEP) coding  
    m_vitdec->reset();
    {
        size_t N;
        auto symbols_buf = tcb::span(m_encoded_bits_buf);
        for (int i = 0; i < EEP_Descriptor::TOTAL_PUNCTURE_CODES; i++) {
            const int Lx = descriptor.Lx[i].GetLx(n);
            const auto puncture_code = GetPunctureCode(descriptor.PIx[i]);
            N = m_vitdec->update(symbols_buf, puncture_code, 128*Lx);
            symbols_buf = symbols_buf.subspan(N);
        }
        N = m_vitdec->update(symbols_buf, PI_X, 24);
        symbols_buf = symbols_buf.subspan(N);
        assert(symbols_buf.size() == 0);
    }


    const int curr_decoded_bit = int(m_vitdec->get_current_decoded_bit());
    const int nb_tail_bits = 24/int(DAB_Viterbi_Decoder::m_code_rate);
    const int nb_decoded_bits = curr_decoded_bit-nb_tail_bits;
    const int nb_decoded_bytes = nb_decoded_bits/8;
    const uint64_t error = m_vitdec->chainback({m_decoded_bytes_buf.data(), (size_t)nb_decoded_bytes});
    LOG_MESSAGE("vitdec_error: {}", error);

    // descrambler
    m_scrambler->Reset();
    for (int i = 0; i < nb_decoded_bytes; i++) {
        uint8_t b = m_scrambler->Process();
        m_decoded_bytes_buf[i] ^= b;
    }

    return nb_decoded_bytes;
}

// TODO: We don't have any samples to test if UEP decoding works
int MSC_Decoder::DecodeUEP() {
    const auto descriptor = GetUEPDescriptor(m_subchannel);

    // DOC: ETSI EN 300 401
    // Clause 11.3.1 - Unequal Error Protection (UEP) coding 
    m_vitdec->reset();
    {
        size_t N = 0u;
        auto symbols_buf = tcb::span(m_encoded_bits_buf);
        for (int i = 0; i < UEP_Descriptor::TOTAL_PUNCTURE_CODES; i++) {
            const int Lx = descriptor.Lx[i];
            const auto puncture_code = GetPunctureCode(descriptor.PIx[i]);
            N = m_vitdec->update(symbols_buf, puncture_code, 128*Lx);
            symbols_buf = symbols_buf.subspan(N);
        }
        N = m_vitdec->update(symbols_buf, PI_X, 24);
        symbols_buf = symbols_buf.subspan(N);
    }

    const int curr_decoded_bit = int(m_vitdec->get_current_decoded_bit());
    const int nb_tail_bits = 24/int(DAB_Viterbi_Decoder::m_code_rate);
    // const int nb_padding_bits = (int)descriptor.total_padding_bits;
    const int nb_decoded_bits = curr_decoded_bit-nb_tail_bits;
    assert(nb_decoded_bits % 8 == 0);
    const int nb_decoded_bytes = nb_decoded_bits/8;
    const uint64_t error = m_vitdec->chainback({m_decoded_bytes_buf.data(), (size_t)nb_decoded_bytes});
    LOG_MESSAGE("vitdec_error: {}", error);

    // descrambler
    m_scrambler->Reset();
    for (int i = 0; i < nb_decoded_bytes; i++) {
        uint8_t b = m_scrambler->Process();
        m_decoded_bytes_buf[i] ^= b;
    }

    return nb_decoded_bytes;
}