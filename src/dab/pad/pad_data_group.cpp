#include "pad_data_group.h"
#include "algorithms/crc.h"

#include <stdio.h>
#define LOG_MESSAGE(fmt, ...) fprintf(stderr, "[pad-data-group] " fmt "\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) fprintf(stderr, "ERROR: [pad-data-group] " fmt "\n", ##__VA_ARGS__)

static const auto Generate_CRC_Calc() {
    // DOC: ETSI EN 300 401
    // Clause 7.4.5 - Applications in XPAD
    // Clause 7.4.5.0 - Introduction
    // CRC16 Polynomial is given by:
    // G(x) = x^16 + x^12 + x^5 + 1
    // POLY = 0b 0001 0000 0010 0001 = 0x1021
    static const uint16_t crc16_poly = 0x1021;
    static auto crc16_calc = new CRC_Calculator<uint16_t>(crc16_poly);
    crc16_calc->SetInitialValue(0xFFFF);    // initial value all 1s
    crc16_calc->SetFinalXORValue(0xFFFF);   // transmitted crc is 1s complemented

    return crc16_calc;
};

static auto CRC16_CALC = Generate_CRC_Calc();

int PAD_Data_Group::Consume(const uint8_t* data, const int N) {
    const int nb_remain = nb_required_bytes - nb_curr_bytes;
    const int nb_read = (nb_remain > N) ? N : nb_remain;
    for (int i = 0; i < nb_read; i++) {
        buffer[nb_curr_bytes++] = data[i];
    }
    return nb_read;
}

bool PAD_Data_Group::CheckCRC(void) {
    const int MIN_CRC_BYTES = 2;
    if (nb_required_bytes < MIN_CRC_BYTES) {
        return false;
    }

    const auto* buf = buffer.data();
    const int N = nb_required_bytes;
    const int nb_data_bytes = N-MIN_CRC_BYTES;

    const uint16_t crc16_rx = (buf[N-2] << 8) | buf[N-1];
    const uint16_t crc16_calc = CRC16_CALC->Process(buf, nb_data_bytes);

    const bool is_match = (crc16_rx == crc16_calc);
    // if (!is_match) {
    //     LOG_ERROR("Doesn't match rx=%04X pred=%04X", 
    //         crc16_rx, crc16_calc);
    // }

    return is_match;
}

void PAD_Data_Group::Reset(void) {
    nb_required_bytes = 0;
    nb_curr_bytes = 0;
    buffer.resize(0);
    buffer.clear();
}