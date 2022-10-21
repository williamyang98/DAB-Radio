#include "aac_frame_processor.h"
#include "algorithms/reed_solomon_decoder.h"
#include "algorithms/crc.h"

#include "aac_decoder.h"

#include "easylogging++.h"
#include "fmt/core.h"

#define LOG_MESSAGE(...) CLOG(INFO, "aac-frame") << fmt::format(##__VA_ARGS__)
#define LOG_ERROR(...) CLOG(ERROR, "aac-frame") << fmt::format(##__VA_ARGS__)

constexpr int MAX_SUPER_FRAME_SIZE = 10000;

// Reed solomon decoder paramters
constexpr int NB_RS_MESSAGE_BYTES = 120;
constexpr int NB_RS_DATA_BYTES    = 110;
constexpr int NB_RS_PARITY_BYTES  = 10;

// Helper function to read 12bit segments from a buffer
// It returns the number of bytes that were read (rounded up to nearest byte)
int read_au_start(const uint8_t* buf, uint16_t* data, const int N) {
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

AAC_Frame_Processor::AAC_Frame_Processor() {
    // For the following polynomials refer to ETSI TS 102 563 

    // Refer to the section below table 2 in clause 5.2
    // Generator polynomial for the the fire code
    // G(x) = (x^11 + 1) * (x^5 + x^3 + x^2 + x^1 + 1) 
    // G(x) = x^16 + x^14 + x^13 + x^12 + x^11 + x^5 + x^3 + x^2 + x^1 + 1
    const uint16_t firecode_poly = 0b0111100000101111;
    firecode_crc_calc = new CRC_Calculator<uint16_t>(firecode_poly);
    firecode_crc_calc->SetInitialValue(0x0000);
    firecode_crc_calc->SetFinalXORValue(0x0000);

    // Refer to the section below table 1 in clause 5.2
    // Generator polynomial for the access unit crc check
    // G(x) = x^16 + x^12 + x^5 + 1
    // initial = all 1s, complement = true
    const uint16_t au_crc_poly = 0b0001000000100001;
    access_unit_crc_calc = new CRC_Calculator<uint16_t>(au_crc_poly);
    access_unit_crc_calc->SetInitialValue(0xFFFF);
    access_unit_crc_calc->SetFinalXORValue(0xFFFF);

    // Refer to clause 6.1 on reed solomon coding
    // The polynomial for this is given as
    // G(x) = x^8 + x^4 + x^3 + x^2 + 1
    const int galois_field_poly = 0b100011101;
    // The Phil Karn reed solmon decoder works with the 2^8 Galois field
    // Therefore we need to use the RS(255,245) decoder
    // As according to the spec we should insert 135 padding bits
    const int nb_padding_bits = 255-NB_RS_MESSAGE_BYTES;
    rs_decoder = new Reed_Solomon_Decoder(8, galois_field_poly, 0, 1, 10, nb_padding_bits);
    rs_encoded_buf = new uint8_t[NB_RS_MESSAGE_BYTES]{0};
    // Reed solomon code can correct up to floor(t/2) symbols that were wrong
    // where t = the number of parity symbols
    rs_error_positions = new int[NB_RS_PARITY_BYTES]{0};

    state = State::WAIT_FRAME_START;
    curr_dab_frame = 0;
    super_frame_buf = new uint8_t[MAX_SUPER_FRAME_SIZE];
}

AAC_Frame_Processor::~AAC_Frame_Processor() {
    delete firecode_crc_calc;
    delete access_unit_crc_calc;
    delete super_frame_buf;
    if (aac_decoder != NULL) {
        delete aac_decoder;
    }

    delete rs_decoder;
    delete [] rs_encoded_buf;
    delete [] rs_error_positions;
}

void AAC_Frame_Processor::Process(const uint8_t* buf, const int N) {
    // If the buffer size changed reset our accumulated DAB logical frames
    if (prev_nb_dab_frame_bytes != N) {
        prev_nb_dab_frame_bytes = N;
        curr_dab_frame = 0;
        state = State::WAIT_FRAME_START;
    }

    if (state == State::WAIT_FRAME_START) {
        if (!CalculateFirecode(buf, N)) {
            return;
        }
        state = State::COLLECT_FRAMES;
        curr_dab_frame = 0;
    }

    AccumulateFrame(buf, N);
    curr_dab_frame++;

    if (curr_dab_frame == TOTAL_DAB_FRAMES) {
        ProcessSuperFrame(N);
        state = State::WAIT_FRAME_START;
    }
}

bool AAC_Frame_Processor::CalculateFirecode(const uint8_t* buf, const int N) {
    auto* crc_data = &buf[2];
    const int nb_crc_bytes = 9;
    const uint16_t crc_rx = (buf[0] << 8) | buf[1];
    const uint16_t crc_pred = firecode_crc_calc->Process(crc_data, nb_crc_bytes);
    const bool is_valid = (crc_rx == crc_pred);
    LOG_MESSAGE("[crc16] [firecode] is_match={} got={:04X} calc={:04X}",
        is_valid, crc_rx, crc_pred);
    
    if (!is_valid) {
        obs_firecode_error.Notify(curr_dab_frame, crc_rx, crc_pred);
    }

    return is_valid;
}

void AAC_Frame_Processor::AccumulateFrame(const uint8_t* buf, const int N) {
    auto* dst_buf = &super_frame_buf[curr_dab_frame*N];
    for (int i = 0; i < N; i++) {
        dst_buf[i] = buf[i];
    }
}

void AAC_Frame_Processor::ProcessSuperFrame(const int nb_dab_frame_bytes) {
    const int nb_rs_super_frame_bytes = nb_dab_frame_bytes*TOTAL_DAB_FRAMES;
    const int N = nb_rs_super_frame_bytes/NB_RS_MESSAGE_BYTES;

    // reed solomon decoder
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < NB_RS_MESSAGE_BYTES; j++) {
            rs_encoded_buf[j] = super_frame_buf[i + j*N];
        }
        const int error_count = rs_decoder->Decode(
            rs_encoded_buf, rs_error_positions, 0);

        LOG_MESSAGE("[reed-solomon] index={} error_count={}", i, error_count);
        // rs decoder returns -1 to indicate too many errors
        if (error_count < 0) {
            LOG_ERROR("Too many errors for reed solomon to correct");
            obs_rs_error.Notify(i, N);
            return;
        }
        // correct any errors
        for (int j = 0; j < error_count; j++) {
            const int k = rs_error_positions[j];
            if (k < 0) {
                continue;
            }
            super_frame_buf[i + k*N] = rs_encoded_buf[k];
        }
    }

    // Decode audio superframe header
    auto* buf = super_frame_buf;
    int curr_byte = 0;
    const uint16_t firecode = (buf[0] << 8) | (buf[1]);
    const uint8_t descriptor = buf[2];
    const uint8_t rfa               = (descriptor & 0b10000000) >> 7;
    const uint8_t dac_rate          = (descriptor & 0b01000000) >> 6;
    const uint8_t sbr_flag          = (descriptor & 0b00100000) >> 5;
    const uint8_t aac_channel_mode  = (descriptor & 0b00010000) >> 4;
    const uint8_t ps_flag           = (descriptor & 0b00001000) >> 3;
    const uint8_t mpeg_config       = (descriptor & 0b00000111) >> 0;

    const uint32_t sampling_rate = dac_rate ? 48000 : 32000;
    const bool is_stereo = aac_channel_mode;

    LOG_MESSAGE("sampling_rate={} Hz", sampling_rate);
    LOG_MESSAGE("sbr={}", sbr_flag);
    LOG_MESSAGE("ps={}", ps_flag);
    LOG_MESSAGE("is_stereo={}", is_stereo);

    // TODO: Somehow handle the mpeg configuration
    // libfaad allows you to set more advanced audio channel configuration
    switch (mpeg_config) {
    case 0b000:
        LOG_MESSAGE("MPEG surround is not used");
        break;
    case 0b001:
        LOG_MESSAGE("MPEG surround with 5.1 output channels is used");
        break;
    case 0b111:
        LOG_MESSAGE("MPEG surround in other mode");
        break;
    default:
        LOG_MESSAGE("MPEG surround is RFA");
        break;
    }

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

    // keep track if the superframe header changed
    const bool is_format_changed = (prev_superframe_descriptor != descriptor);
    prev_superframe_descriptor = descriptor;

    // update the aac decoder if the parameters change
    if ((aac_decoder == NULL) || is_format_changed) {
        AAC_Decoder::Params params;
        params.sampling_frequency = sampling_rate;
        params.is_PS = ps_flag;
        params.is_SBR = sbr_flag;
        params.is_stereo = is_stereo;

        if (aac_decoder != NULL) {
            delete aac_decoder;
            aac_decoder = NULL;
            LOG_ERROR("AAC decoder parameters changed: sampling_rate={}Hz PS={} SBR={} stereo={}", 
                sampling_rate, ps_flag, sbr_flag, is_stereo);
        } else {
            LOG_MESSAGE("AAC decoder parameters initialised: sampling_rate={}Hz PS={} SBR={} stereo={}", 
                sampling_rate, ps_flag, sbr_flag, is_stereo);
        }
        aac_decoder = new AAC_Decoder(params);
    }

    // process each access unit through the AAC decoder
    for (int i = 0; i < num_aus; i++) {
        const uint16_t nb_au_bytes = au_start[i+1] - au_start[i];
        uint8_t* au_buf = &buf[au_start[i]];

        const auto nb_crc_bytes = sizeof(uint16_t);
        const uint16_t nb_data_bytes = nb_au_bytes - nb_crc_bytes;
        const uint8_t* crc_buf = &au_buf[nb_data_bytes];
        const uint16_t crc_rx = (crc_buf[0] << 8) | crc_buf[1];


        const uint16_t crc_pred = access_unit_crc_calc->Process(au_buf, nb_data_bytes);
        const bool is_crc_valid = (crc_pred == crc_rx);
        LOG_MESSAGE("[crc16] au={} is_match={} crc_pred={:04X} crc_rx={:04X}", i, is_crc_valid, crc_pred, crc_rx);

        if (!is_crc_valid) {
            obs_au_crc_error.Notify(i, num_aus, crc_rx, crc_pred);
            continue;
        }

        const auto res = aac_decoder->DecodeFrame(au_buf, nb_data_bytes);
        LOG_MESSAGE("[aac-decoder] error={} error_code={} total_audio_bytes={}", 
            res.is_error, res.error_code, res.nb_audio_buf_bytes);

        if (res.is_error) {
            obs_au_decoder_error.Notify(i, num_aus, res.error_code);
        } else {
            obs_au_audio_frame.Notify(
                i, num_aus, 
                res.audio_buf, res.nb_audio_buf_bytes,
                aac_decoder->GetParams());
        }
    }
}