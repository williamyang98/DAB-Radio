#pragma once
#include "algorithms/crc.h"

class AAC_Decoder;

class AAC_Frame_Processor 
{
public:
    enum MPEG_Config { NONE, SURROUND_51, SURROUND_OTHER };
private:
    enum State { WAIT_FRAME_START, COLLECT_FRAMES };
private:
    CRC_Calculator<uint16_t>* firecode_crc_calc;
    CRC_Calculator<uint16_t>* access_unit_crc_calc;
    State state;
    const int TOTAL_DAB_FRAMES = 5;
    int curr_dab_frame = 0;
    uint8_t* super_frame_buf;
    AAC_Decoder* aac_decoder = NULL;
    int prev_nb_dab_frame_bytes = 0;
    uint8_t prev_superframe_descriptor = 0x00;
public:
    AAC_Frame_Processor();
    ~AAC_Frame_Processor();
    // A audio super frame consists of 5 DAB logical frames
    void Process(const uint8_t* buf, const int N);
private:
    bool CalculateFirecode(const uint8_t* buf, const int N);
    void AccumulateFrame(const uint8_t* buf, const int N);
    void ProcessSuperFrame(const int nb_dab_frame_bytes);
};