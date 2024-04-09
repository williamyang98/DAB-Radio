#include "./aac_frame_processor.h"
#include <stddef.h>
#include <stdint.h>
#include <memory>
#include <fmt/format.h>
#include "utility/span.h"
#include "../algorithms/crc.h"
#include "../algorithms/reed_solomon_decoder.h"
#include "../dab_logging.h"
#define TAG "aac-frame-processor"
static auto _logger = DAB_LOG_REGISTER(TAG);
#define LOG_MESSAGE(...) DAB_LOG_MESSAGE(TAG, fmt::format(__VA_ARGS__))
#define LOG_ERROR(...) DAB_LOG_ERROR(TAG, fmt::format(__VA_ARGS__))

constexpr int NB_FIRECODE_CRC16_BYTES = 2;
constexpr int NB_FIRECODE_DATA_BYTES = 9;
constexpr int MIN_DAB_LOGICAL_FRAME_SIZE = NB_FIRECODE_CRC16_BYTES+NB_FIRECODE_DATA_BYTES;

// Reed solomon decoder paramters
constexpr int NB_RS_MESSAGE_BYTES = 120;
constexpr int NB_RS_DATA_BYTES    = 110;
constexpr int NB_RS_PARITY_BYTES  = 10;

// NOTE: We need to pad the RS(120,110) code to RS(255,245)
// This is done by padding 135 zero symbols to the left of the message
constexpr int NB_RS_PADDING_BYTES = 255 - NB_RS_MESSAGE_BYTES;

// Helper function to read 12bit segments from a buffer
// It returns the number of bytes that were read (rounded up to nearest byte)
static int read_au_start(const uint8_t* buf, uint16_t* data, const int N) {
    int curr_value = 0;
    int curr_value_bits = 0;
    const int nb_total_bits = 12;

    int curr_byte = 0;
    int remain_bits = 8;

    for (int i = 0; i < N; i++) {
        data[i] = 0;
    }

    while (curr_value < N) {
        auto& v = data[curr_value];
        const int nb_required_bits = nb_total_bits-curr_value_bits;
        const int nb_consume_bits = (remain_bits > nb_required_bits) ? nb_required_bits : remain_bits;

        const uint8_t b = buf[curr_byte];

        const int remove_shift = 8-remain_bits;
        const uint8_t masked_b = ((b << remove_shift) & 0xFF) >> remove_shift;

        v = (v << nb_consume_bits) | (masked_b >> (remain_bits-nb_consume_bits));
        remain_bits -= nb_consume_bits;
        curr_value_bits += nb_consume_bits;

        if (remain_bits == 0) {
            remain_bits = 8;
            curr_byte++;
        }

        if (curr_value_bits == nb_total_bits) {
            curr_value_bits = 0;
            curr_value++;
        }
    }

    // pad out remaining bits to get byte alignment
    if (remain_bits < 8) {
        curr_byte++;
    }
    return curr_byte;
}

static auto FIRECODE_CRC_CALC = []() {
    // DOC: ETSI TS 102 563 
    // Refer to the section below table 2 in clause 5.2
    // Generator polynomial for the the fire code
    // G(x) = (x^11 + 1) * (x^5 + x^3 + x^2 + x^1 + 1) 
    // G(x) = x^16 + x^14 + x^13 + x^12 + x^11 + x^5 + x^3 + x^2 + x^1 + 1
    const uint16_t firecode_poly = 0b0111100000101111;
    auto calc = new CRC_Calculator<uint16_t>(firecode_poly);
    calc->SetInitialValue(0x0000);
    calc->SetFinalXORValue(0x0000);
    return calc;
} ();

static auto ACCESS_UNIT_CRC_CALC = []() {
    // DOC: ETSI TS 102 563 
    // Refer to the section below table 1 in clause 5.2
    // Generator polynomial for the access unit crc check
    // G(x) = x^16 + x^12 + x^5 + 1
    // initial = all 1s, complement = true
    const uint16_t au_crc_poly = 0b0001000000100001;
    auto calc = new CRC_Calculator<uint16_t>(au_crc_poly);
    calc->SetInitialValue(0xFFFF);
    calc->SetFinalXORValue(0xFFFF);
    return calc;
} ();

AAC_Frame_Processor::AAC_Frame_Processor() {
    // DOC: ETSI TS 102 563 
    // Refer to clause 6.1 on reed solomon coding
    // The polynomial for this is given as
    // P(x) = x^8 + x^4 + x^3 + x^2 + 1
    const int GALOIS_FIELD_POLY = 0b100011101;
    // G(x) = (x+λ^0)*(x+λ^1)*...*(x+λ^9)
    const int CODE_TOTAL_ROOTS = 10;
    // The Phil Karn reed solmon decoder works with the 2^8 Galois field
    // Therefore we need to use the RS(255,245) decoder
    // As according to the spec we should insert 135 padding symbols (bytes)
    m_rs_decoder = std::make_unique<Reed_Solomon_Decoder>(8, GALOIS_FIELD_POLY, 0, 1, CODE_TOTAL_ROOTS, NB_RS_PADDING_BYTES);
    m_rs_encoded_buf.resize(NB_RS_MESSAGE_BYTES, 0);
    // Reed solomon code can correct up to floor(t/2) symbols that were wrong
    // where t = the number of parity symbols
    m_rs_error_positions.resize(NB_RS_PARITY_BYTES, 0);

    m_state = State::WAIT_FRAME_START;
    m_curr_dab_frame = 0;
    m_prev_nb_dab_frame_bytes = 0;
    m_is_synced_superframe = false;
    m_nb_desync_count = 0;
}

AAC_Frame_Processor::~AAC_Frame_Processor() = default;

void AAC_Frame_Processor::Process(tcb::span<const uint8_t> buf) {
    const int N = (int)buf.size();
    if (N == 0) { 
        LOG_ERROR("Received an empty buffer");
        return;
    }

    if (N < MIN_DAB_LOGICAL_FRAME_SIZE) {
        LOG_ERROR("DAB frame is of insufficient size %d<%d", N, MIN_DAB_LOGICAL_FRAME_SIZE);
        return;
    }

    // If the buffer size changed reset our accumulated DAB logical frames
    if (m_prev_nb_dab_frame_bytes != N) {
        if (m_prev_nb_dab_frame_bytes != 0) {
            LOG_ERROR("Unexpected resize of DAB logical frame %d!=%d", m_prev_nb_dab_frame_bytes, N);
        }
        m_prev_nb_dab_frame_bytes = N;
        m_super_frame_buf.resize(m_TOTAL_DAB_FRAMES*N);
        m_curr_dab_frame = 0;
        m_state = State::WAIT_FRAME_START;
    }

    // if our superframes fail validation too many times
    // then we resort to waiting for the firecode to be valid
    if (m_nb_desync_count >= m_nb_desync_max_count) {
        m_nb_desync_count = 0;
        m_is_synced_superframe = false;
    }

    // if we are synced to the superframe
    // then skip non reed solomon corrected firecode search
    if (m_is_synced_superframe) {
        m_state = State::COLLECT_FRAMES;
    }

    if (m_state == State::WAIT_FRAME_START) {
        if (!CalculateFirecode(buf)) {
            return;
        }
        m_state = State::COLLECT_FRAMES;
    }

    AccumulateFrame(buf);
    m_curr_dab_frame++;

    if (m_curr_dab_frame == m_TOTAL_DAB_FRAMES) {
        ProcessSuperFrame(N);
        m_state = State::WAIT_FRAME_START;
        m_curr_dab_frame = 0;
    }
}

bool AAC_Frame_Processor::CalculateFirecode(tcb::span<const uint8_t> buf) {
    auto crc_data = buf.subspan(NB_FIRECODE_CRC16_BYTES, NB_FIRECODE_DATA_BYTES);
    const uint16_t crc_rx = (buf[0] << 8) | buf[1];
    const uint16_t crc_pred = FIRECODE_CRC_CALC->Process(crc_data);
    const bool is_valid = (crc_rx == crc_pred);
    LOG_MESSAGE("[crc16] [firecode] is_match={} got={:04X} calc={:04X}", is_valid, crc_rx, crc_pred);

    if (!is_valid) {
        m_obs_firecode_error.Notify(m_curr_dab_frame, crc_rx, crc_pred);
    }

    return is_valid;
}

void AAC_Frame_Processor::AccumulateFrame(tcb::span<const uint8_t> buf) {
    const size_t N = buf.size();
    auto dst_buf = tcb::span(m_super_frame_buf).subspan(m_curr_dab_frame*N, N);
    for (size_t i = 0; i < N; i++) {
        dst_buf[i] = buf[i];
    }
}

void AAC_Frame_Processor::ProcessSuperFrame(const int nb_dab_frame_bytes) {
    const int nb_rs_super_frame_bytes = nb_dab_frame_bytes*m_TOTAL_DAB_FRAMES;
    const int N = nb_rs_super_frame_bytes/NB_RS_MESSAGE_BYTES;

    if (!ReedSolomonDecode(nb_dab_frame_bytes)) {
        m_nb_desync_count++;
        return;
    }

    if (!CalculateFirecode(m_super_frame_buf)) {
        m_nb_desync_count++;
        return;
    }

    // if validated, reset resynchronisation counter
    m_nb_desync_count = 0;
    m_is_synced_superframe = true;

    // Decode audio superframe header
    // DOC: ETSI TS 102 563
    // Clause 5.2: Audio super framing syntax 
    // Table 2: Syntax of he_aac_super_frame_header() 
    auto& buf = m_super_frame_buf;
    int curr_byte = 0;
    // TODO: We can fix firecode using ECC properties
    // const uint16_t firecode = (buf[0] << 8) | (buf[1]);
    const uint8_t descriptor = buf[2];
    // const uint8_t rfa               = (descriptor & 0b10000000) >> 7;
    const uint8_t dac_rate          = (descriptor & 0b01000000) >> 6;
    const uint8_t sbr_flag          = (descriptor & 0b00100000) >> 5;
    const uint8_t aac_channel_mode  = (descriptor & 0b00010000) >> 4;
    const uint8_t ps_flag           = (descriptor & 0b00001000) >> 3;
    const uint8_t mpeg_config       = (descriptor & 0b00000111) >> 0;

    const uint32_t sampling_rate = dac_rate ? 48000 : 32000;
    const bool is_stereo = aac_channel_mode;

    SuperFrameHeader super_frame_header;
    super_frame_header.sampling_rate = sampling_rate;
    super_frame_header.PS_flag = ps_flag;
    super_frame_header.SBR_flag = sbr_flag;
    super_frame_header.is_stereo = is_stereo;

    // TODO: Somehow handle the mpeg configuration
    // libfaad allows you to set more advanced audio channel configuration
    switch (mpeg_config) {
    case 0b000:
        super_frame_header.mpeg_surround = MPEG_Surround::NOT_USED;
        LOG_MESSAGE("MPEG surround is not used");
        break;
    case 0b001:
        super_frame_header.mpeg_surround = MPEG_Surround::SURROUND_51;
        LOG_MESSAGE("MPEG surround with 5.1 output channels is used");
        break;
    case 0b111:
        super_frame_header.mpeg_surround = MPEG_Surround::SURROUND_OTHER;
        LOG_MESSAGE("MPEG surround in other mode");
        break;
    default:
        super_frame_header.mpeg_surround = MPEG_Surround::RFA;
        LOG_MESSAGE("MPEG surround is RFA");
        break;
    }

    m_obs_superframe_header.Notify(super_frame_header);
    LOG_MESSAGE("AAC decoder parameters: sampling_rate={}Hz PS={} SBR={} stereo={}", 
        sampling_rate, ps_flag, sbr_flag, is_stereo);

    // Get the starting byte index for each AU (access unit) in the super frame
    uint8_t num_aus = 0;
    if ((dac_rate == 0) && (sbr_flag == 1)) num_aus = 2;
    if ((dac_rate == 1) && (sbr_flag == 1)) num_aus = 3;
    if ((dac_rate == 0) && (sbr_flag == 0)) num_aus = 4;
    if ((dac_rate == 1) && (sbr_flag == 0)) num_aus = 6;
    uint16_t au_start[7] = {0};
    const int nb_au_start_bytes = read_au_start(&buf[3], &au_start[1], num_aus-1);
    au_start[num_aus] = NB_RS_DATA_BYTES*N;

    // size of the audio descriptor fields
    curr_byte += 3;
    curr_byte += nb_au_start_bytes;
    // the first access unit doesn't have the starting index specified
    // it begins immediately after the superframe header
    au_start[0] = curr_byte;

    // process each access unit through the AAC decoder
    for (int i = 0; i < num_aus; i++) {
        const int nb_au_bytes = (int)au_start[i+1] - (int)au_start[i];
        const int nb_crc_bytes = (int)sizeof(uint16_t);
        const int nb_data_bytes = nb_au_bytes - nb_crc_bytes;
        if ((nb_data_bytes < 0) || (au_start[i+1] >= buf.size())) {
            LOG_ERROR("access unit out of bounds: i=%d/%d range=[%d,%d] N=%d", 
                i, num_aus, 
                (int)au_start[i], (int)au_start[i+1],
                buf.size());
            return;
        }

        auto au_buf = tcb::span(buf).subspan(au_start[i], nb_au_bytes);
        auto data_buf = au_buf.first(nb_data_bytes);
        auto crc_buf = au_buf.last(nb_crc_bytes);

        const uint16_t crc_rx = (crc_buf[0] << 8) | crc_buf[1];
        const uint16_t crc_pred = ACCESS_UNIT_CRC_CALC->Process(data_buf);
        const bool is_crc_valid = (crc_pred == crc_rx);
        LOG_MESSAGE("[crc16] au={} is_match={} crc_pred={:04X} crc_rx={:04X}", i, is_crc_valid, crc_pred, crc_rx);

        if (!is_crc_valid) {
            m_obs_au_crc_error.Notify(i, num_aus, crc_rx, crc_pred);
            continue;
        }

        m_obs_access_unit.Notify(i, num_aus, data_buf);
    }
}

bool AAC_Frame_Processor::ReedSolomonDecode(const int nb_dab_frame_bytes) {
    const int nb_rs_super_frame_bytes = nb_dab_frame_bytes*m_TOTAL_DAB_FRAMES;
    const int N = nb_rs_super_frame_bytes/NB_RS_MESSAGE_BYTES;

    // DOC: ETSI TS 102 563
    // Clause 6: Transport error coding and interleaving 
    // We need to interleave the data so we can perform Reed Solomon decoding
    // Then we deinterleave the corrected RS data into the super frame buffer

    // reed solomon decoder
    for (int i = 0; i < N; i++) {
        // Interleave for decoding
        for (int j = 0; j < NB_RS_MESSAGE_BYTES; j++) {
            m_rs_encoded_buf[j] = m_super_frame_buf[i + j*N];
        }
        const int error_count = m_rs_decoder->Decode(
            m_rs_encoded_buf.data(), m_rs_error_positions.data(), 0);

        LOG_MESSAGE("[reed-solomon] index={}/{} error_count={}", i, N, error_count);
        // rs decoder returns -1 to indicate too many errors
        if (error_count < 0) {
            LOG_ERROR("Too many errors for reed solomon to correct");
            m_obs_rs_error.Notify(i, N);
            return false;
        }
        // correct any errors
        for (int j = 0; j < error_count; j++) {
            // NOTE: Phil Karn's reed solmon decoder returns the position of errors 
            // with the amount of padding added onto it
            const int k = m_rs_error_positions[j] - NB_RS_PADDING_BYTES;
            if (k < 0) {
                LOG_ERROR("[reed-solomon] Got a negative error index {} in DAB frame {}/{}", k, i, N);
                continue;
            }
            // Deinterleave for error correction
            m_super_frame_buf[i + k*N] = m_rs_encoded_buf[k];
        }
    }

    return true;
}