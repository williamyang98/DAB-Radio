#include "pad_data_length_indicator.h"

#include "easylogging++.h"
#include "fmt/core.h"

#define LOG_MESSAGE(...) CLOG(INFO, "pad-data-length") << fmt::format(##__VA_ARGS__)
#define LOG_ERROR(...) CLOG(ERROR, "pad-data-length") << fmt::format(##__VA_ARGS__)

constexpr int TOTAL_DATA_GROUP_BYTES = 4;

PAD_Data_Length_Indicator::PAD_Data_Length_Indicator() {
    data_group.Reset();
    data_group.SetRequiredBytes(TOTAL_DATA_GROUP_BYTES);

    is_length_available = false;
    length = 0;
}

void PAD_Data_Length_Indicator::ResetLength(void) { 
    is_length_available = false;
    length = 0;
    data_group.Reset();
    data_group.SetRequiredBytes(TOTAL_DATA_GROUP_BYTES);
}

void PAD_Data_Length_Indicator::ProcessXPAD(const uint8_t* buf, const int N) {
    int curr_byte = 0;
    while (curr_byte < N) {
        const int nb_remain = N-curr_byte;
        const int nb_read = Consume(&buf[curr_byte], nb_remain);
        curr_byte += nb_read;
    }
}

int PAD_Data_Length_Indicator::Consume(const uint8_t* buf, const int N) {
    const int nb_read = data_group.Consume(buf, N);
    LOG_MESSAGE("Progress partial data group {}/{}", data_group.GetCurrentBytes(), data_group.GetRequiredBytes());

    if (!data_group.IsComplete()) {
        return nb_read;
    }

    if (!data_group.CheckCRC()) {
        LOG_ERROR("CRC mismatch on data group");
        data_group.Reset();
        data_group.SetRequiredBytes(TOTAL_DATA_GROUP_BYTES);
        return nb_read;
    }

    Interpret();
    data_group.Reset();
    data_group.SetRequiredBytes(TOTAL_DATA_GROUP_BYTES);
    return nb_read;
}

void PAD_Data_Length_Indicator::Interpret(void) {
    // DOC: ETSI EN 300 401
    // Clause 7.4.5.1.1: X-PAD data group for data group length indicator 
    // Figure 34: Structure of the X-PAD data group for the data group length indicator 
    auto* buf = data_group.GetData();
    const int N = data_group.GetRequiredBytes();

    const uint8_t rfa      =  (buf[0] & 0b11000000) >> 6;
    const uint16_t _length = ((buf[0] & 0b00111111) << 8) |
                             ((buf[1] & 0b11111111) >> 0);
    
    length = _length;
    is_length_available = true;
    LOG_MESSAGE("length={} rfa={}", length, rfa);
}