#pragma once
#include "algorithms/crc.h"
#include "../observable.h"

#include "aac_decoder.h"

class Reed_Solomon_Decoder;

struct AAC_Frame_Processor_Callback {
    // Superframe synchronisation
};

class AAC_Frame_Processor 
{
public:
    enum MPEG_Config { NONE, SURROUND_51, SURROUND_OTHER };
private:
    enum State { WAIT_FRAME_START, COLLECT_FRAMES };
private:
    CRC_Calculator<uint16_t>* firecode_crc_calc;
    CRC_Calculator<uint16_t>* access_unit_crc_calc;
    AAC_Decoder* aac_decoder = NULL;
    Reed_Solomon_Decoder* rs_decoder = NULL;
    uint8_t* rs_encoded_buf = NULL;
    int* rs_error_positions = NULL;
    // superframe acquisition state
    State state;
    const int TOTAL_DAB_FRAMES = 5;
    int curr_dab_frame = 0;
    uint8_t* super_frame_buf;
    int prev_nb_dab_frame_bytes = 0;
    uint8_t prev_superframe_descriptor = 0x00;
    bool is_synced_superframe = false;
    int nb_desync_count = 0;
    const int nb_desync_max_count = 10;
    // callback signatures
    // frame_index, crc_got, crc_calculated
    Observable<const int, const uint16_t, const uint16_t> obs_firecode_error;
    // rs_frame_index, rs_total_frames
    Observable<const int, const int> obs_rs_error;
    // au_index, total_aus, decoder_error_value
    Observable<const int, const int , const int> obs_au_decoder_error;
    // au_index, total_aus, crc_got, crc_calculated
    Observable<const int, const int, const uint16_t, const uint16_t> obs_au_crc_error;
    // au_index, total_aus, audio_buf, nb_audio_buf_bytes, decoder_parameters
    Observable<const int, const int, const uint8_t*, const int, const AAC_Decoder::Params> obs_au_audio_frame;
public:
    AAC_Frame_Processor();
    ~AAC_Frame_Processor();
    // A audio super frame consists of 5 DAB logical frames
    void Process(const uint8_t* buf, const int N);
    auto& OnFirecodeError() { return obs_firecode_error; }
    auto& OnRSError() { return obs_rs_error; }
    auto& OnAUDecoderError() { return obs_au_decoder_error; }
    auto& OnAudioFrame() { return obs_au_audio_frame; }
private:
    bool CalculateFirecode(const uint8_t* buf, const int N);
    void AccumulateFrame(const uint8_t* buf, const int N);
    void ProcessSuperFrame(const int nb_dab_frame_bytes);
};