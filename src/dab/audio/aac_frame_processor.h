#pragma once
#include "../observable.h"

class Reed_Solomon_Decoder;

enum MPEG_Surround {
    NOT_USED, SURROUND_51, SURROUND_OTHER, RFA
};

struct SuperFrameHeader {
    uint32_t sampling_rate;
    bool PS_flag;
    bool SBR_flag;
    bool is_stereo;
    MPEG_Surround mpeg_surround;
};

class AAC_Frame_Processor 
{
public:
    enum MPEG_Config { NONE, SURROUND_51, SURROUND_OTHER };
private:
    enum State { WAIT_FRAME_START, COLLECT_FRAMES };
private:
    Reed_Solomon_Decoder* rs_decoder = NULL;
    uint8_t* rs_encoded_buf = NULL;
    int* rs_error_positions = NULL;
    // superframe acquisition state
    State state;
    const int TOTAL_DAB_FRAMES = 5;
    int curr_dab_frame = 0;
    uint8_t* super_frame_buf;
    int prev_nb_dab_frame_bytes = 0;
    bool is_synced_superframe = false;
    int nb_desync_count = 0;
    const int nb_desync_max_count = 10;
    // callback signatures
    // frame_index, crc_got, crc_calculated
    Observable<const int, const uint16_t, const uint16_t> obs_firecode_error;
    // rs_frame_index, rs_total_frames
    Observable<const int, const int> obs_rs_error;
    // au_index, total_aus, crc_got, crc_calculated
    Observable<const int, const int, const uint16_t, const uint16_t> obs_au_crc_error;
    // superframe_header
    Observable<SuperFrameHeader> obs_superframe_header;
    // au_index, total_aus, au_buffer, nb_au_bytes
    Observable<const int, const int , uint8_t*, const int> obs_access_unit;
public:
    AAC_Frame_Processor();
    ~AAC_Frame_Processor();
    // A audio super frame consists of 5 DAB logical frames
    void Process(const uint8_t* buf, const int N);
    auto& OnFirecodeError(void) { return obs_firecode_error; }
    auto& OnRSError(void) { return obs_rs_error; }
    auto& OnSuperFrameHeader(void) { return obs_superframe_header; }
    auto& OnAccessUnit(void) { return obs_access_unit; }
private:
    bool CalculateFirecode(const uint8_t* buf, const int N);
    void AccumulateFrame(const uint8_t* buf, const int N);
    void ProcessSuperFrame(const int nb_dab_frame_bytes);
private:
    bool ReedSolomonDecode(const int nb_dab_frame_bytes);
};