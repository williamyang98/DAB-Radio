#include "aac_data_decoder.h"

#include "easylogging++.h"
#include "fmt/core.h"

#define LOG_MESSAGE(...) CLOG(INFO, "aac-data-decoder") << fmt::format(##__VA_ARGS__)
#define LOG_ERROR(...) CLOG(ERROR, "aac-data-decoder") << fmt::format(##__VA_ARGS__)

bool AAC_Data_Decoder::ProcessAccessUnit(const uint8_t* data, const int N) {
    const bool is_success = ProcessDataElement(data, N);
    if (!is_success) {
        // DOC: ETSI TS 102 563
        // Clause 5.4.3 PAD extraction
        // If we didn't detect any data stream element then
        // Pad decoder gets: FPAD={0,0}, XPAD=NULL
        uint8_t fpad[2] = {0, 0};
        ProcessPAD(fpad, NULL, 0);
    }
    return is_success;
}

bool AAC_Data_Decoder::ProcessDataElement(const uint8_t* data, const int N) {
    if (N < 2) {
        LOG_ERROR("Data element size too small {}<2", N);
        return false;
    }

    // The standard documentation for this is ISO/IEC 14496-14 which isn't free
    // So we reverse engineer the code from libfaad2
    // Refer to libfaad/syntax.c for details of bit layout of data element
    // Relevant functions are raw_data_block(...), and data_stream_element(...)
    const uint8_t header = data[0];
    const uint8_t data_type     = (header & 0b11100000) >> 5;
    const uint8_t instance_tag  = (header & 0b00011110) >> 1;
    const uint8_t is_byte_align = (header & 0b00000001) >> 0;

    if (data_type == 0) {
        // LOG_ERROR("Got null data type");
        return false;
    } 

    const uint8_t SYNTAX_DATA_STREAM_ELEMENT = 4;
    if (data_type != SYNTAX_DATA_STREAM_ELEMENT) {
        // LOG_ERROR("Got unsupported data type {}!={}", data_type, SYNTAX_DATA_STREAM_ELEMENT);
        return false;
    }

    int curr_byte = 1;
    int length = static_cast<int>(data[curr_byte++]);
    if (length == 255) {
        if (N < 3) {
            LOG_ERROR("Data element size too small for extended length PAD {}<3", N);
            return false;
        }
        length += static_cast<int>(data[curr_byte++]);
    }

    const int nb_remain_bytes = N-curr_byte;
    if (length > nb_remain_bytes) {
        LOG_ERROR("Data stream element size too large {}>{}", length, nb_remain_bytes);
        return false;
    }

    if (length < 2) {
        LOG_ERROR("Insufficient room for the FPAD {} < {}", length, 2);
        return false;
    }

    auto* pad_data = &data[curr_byte];
    // DOC: ETSI TS 102 563
    // Clause 5.4.1: PAD insertion 
    // FPAD is placed at the end of the data stream element
    const int nb_fpad_bytes = 2;
    const int nb_xpad_bytes = length-nb_fpad_bytes;
    auto* xpad_data = &pad_data[0];
    auto* fpad_data = &pad_data[nb_xpad_bytes];
    
    ProcessPAD(fpad_data, xpad_data, nb_xpad_bytes);
    return true;
}

void AAC_Data_Decoder::ProcessPAD(const uint8_t* fpad, const uint8_t* xpad, const int N) {
    pad_processor.Process(fpad, xpad, N);
}