#pragma once
#include <stdint.h>
#include <memory>
#include <vector>
#include "utility/observable.h"
#include "utility/span.h"

class Reed_Solomon_Decoder;

enum class MPEG_Surround {
    NOT_USED, SURROUND_51, SURROUND_OTHER, RFA
};

struct SuperFrameHeader {
    uint32_t sampling_rate = 0;
    bool PS_flag = false;
    bool SBR_flag = false;
    bool is_stereo = false;
    MPEG_Surround mpeg_surround = MPEG_Surround::NOT_USED;
    bool operator==(const SuperFrameHeader& other) {
        return 
            (sampling_rate == other.sampling_rate) &&
            (PS_flag == other.PS_flag) &&
            (SBR_flag == other.SBR_flag) &&
            (is_stereo == other.is_stereo) &&
            (mpeg_surround == other.mpeg_surround);
    }
    bool operator !=(const SuperFrameHeader& other) {
        return !(*this == other);
    }
};

// Reads in DAB main service channel frames
// Reconstructs and decodes AAC superframe
// Outputs superframe header and AAC access units
class AAC_Frame_Processor 
{
public:
    enum class MPEG_Config { NONE, SURROUND_51, SURROUND_OTHER };
private:
    enum class State { WAIT_FRAME_START, COLLECT_FRAMES };
private:
    std::unique_ptr<Reed_Solomon_Decoder> m_rs_decoder;
    std::vector<uint8_t> m_rs_encoded_buf;
    std::vector<int> m_rs_error_positions;
    std::vector<uint8_t> m_super_frame_buf;
    // superframe acquisition state
    State m_state;
    const int m_TOTAL_DAB_FRAMES = 5;
    const int m_nb_desync_max_count = 10;
    int m_curr_dab_frame;
    int m_prev_nb_dab_frame_bytes;
    bool m_is_synced_superframe;
    int m_nb_desync_count;
    // callback signatures
    // frame_index, crc_got, crc_calculated
    Observable<const int, const uint16_t, const uint16_t> m_obs_firecode_error;
    // rs_frame_index, rs_total_frames
    Observable<const int, const int> m_obs_rs_error;
    // superframe_header
    Observable<SuperFrameHeader> m_obs_superframe_header;
    // au_index, total_aus, crc_got, crc_calculated
    Observable<const int, const int, const uint16_t, const uint16_t> m_obs_au_crc_error;
    // au_index, total_aus, au_buffer
    Observable<const int, const int , tcb::span<uint8_t>> m_obs_access_unit;
public:
    AAC_Frame_Processor();
    ~AAC_Frame_Processor();
    // A audio super frame consists of 5 DAB logical frames
    void Process(tcb::span<const uint8_t> buf);
    auto& OnFirecodeError(void) { return m_obs_firecode_error; }
    auto& OnRSError(void) { return m_obs_rs_error; }
    auto& OnSuperFrameHeader(void) { return m_obs_superframe_header; }
    auto& OnAccessUnitCRCError(void) { return m_obs_au_crc_error; }
    auto& OnAccessUnit(void) { return m_obs_access_unit; }
private:
    bool CalculateFirecode(tcb::span<const uint8_t> buf);
    void AccumulateFrame(tcb::span<const uint8_t> buf);
    void ProcessSuperFrame(const int nb_dab_frame_bytes);
private:
    bool ReedSolomonDecode(const int nb_dab_frame_bytes);
};