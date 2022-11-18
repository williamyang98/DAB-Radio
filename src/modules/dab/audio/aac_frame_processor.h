#pragma once
#include <stdint.h>
#include <memory>
#include <vector>
#include "utility/observable.h"
#include "utility/span.h"

class Reed_Solomon_Decoder;

enum MPEG_Surround {
    NOT_USED, SURROUND_51, SURROUND_OTHER, RFA
};

struct SuperFrameHeader {
    uint32_t sampling_rate = 0;
    bool PS_flag = false;
    bool SBR_flag = false;
    bool is_stereo = false;
    MPEG_Surround mpeg_surround = MPEG_Surround::NOT_USED;
};

class AAC_Frame_Processor 
{
public:
    enum MPEG_Config { NONE, SURROUND_51, SURROUND_OTHER };
private:
    enum State { WAIT_FRAME_START, COLLECT_FRAMES };
private:
    std::unique_ptr<Reed_Solomon_Decoder> rs_decoder;
    std::vector<uint8_t> rs_encoded_buf;
    std::vector<int> rs_error_positions;
    std::vector<uint8_t> super_frame_buf;
    // superframe acquisition state
    State state;
    const int TOTAL_DAB_FRAMES = 5;
    int curr_dab_frame = 0;
    int prev_nb_dab_frame_bytes = 0;
    bool is_synced_superframe = false;
    int nb_desync_count = 0;
    const int nb_desync_max_count = 10;
    // callback signatures
    // frame_index, crc_got, crc_calculated
    Observable<const int, const uint16_t, const uint16_t> obs_firecode_error;
    // rs_frame_index, rs_total_frames
    Observable<const int, const int> obs_rs_error;
    // superframe_header
    Observable<SuperFrameHeader> obs_superframe_header;
    // au_index, total_aus, crc_got, crc_calculated
    Observable<const int, const int, const uint16_t, const uint16_t> obs_au_crc_error;
    // au_index, total_aus, au_buffer
    Observable<const int, const int , tcb::span<uint8_t>> obs_access_unit;
public:
    AAC_Frame_Processor();
    ~AAC_Frame_Processor();
    // A audio super frame consists of 5 DAB logical frames
    void Process(tcb::span<const uint8_t> buf);
    auto& OnFirecodeError(void) { return obs_firecode_error; }
    auto& OnRSError(void) { return obs_rs_error; }
    auto& OnSuperFrameHeader(void) { return obs_superframe_header; }
    auto& OnAccessUnitCRCError(void) { return obs_au_crc_error; }
    auto& OnAccessUnit(void) { return obs_access_unit; }
private:
    bool CalculateFirecode(tcb::span<const uint8_t> buf);
    void AccumulateFrame(tcb::span<const uint8_t> buf);
    void ProcessSuperFrame(const int nb_dab_frame_bytes);
private:
    bool ReedSolomonDecode(const int nb_dab_frame_bytes);
};