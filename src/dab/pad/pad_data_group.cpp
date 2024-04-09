#include "./pad_data_group.h"
#include <stddef.h>
#include <stdint.h>
#include "utility/span.h"
#include "../algorithms/crc.h"

static auto Generate_CRC_Calc() {
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

size_t PAD_Data_Group::Consume(tcb::span<const uint8_t> data) {
    const size_t N = data.size();
    const size_t nb_remain = m_nb_required_bytes - m_nb_curr_bytes;
    const size_t nb_read = (nb_remain > N) ? N : nb_remain;
    for (size_t i = 0; i < nb_read; i++) {
        m_buffer[m_nb_curr_bytes++] = data[i];
    }
    return nb_read;
}

bool PAD_Data_Group::CheckCRC(void) {
    const size_t MIN_CRC_BYTES = 2;
    if (m_nb_required_bytes < MIN_CRC_BYTES) {
        return false;
    }

    const auto* buf = m_buffer.data();
    const size_t N = m_nb_required_bytes;
    const size_t nb_data_bytes = N-MIN_CRC_BYTES;

    const uint16_t crc16_rx = (buf[N-2] << 8) | buf[N-1];
    const uint16_t crc16_calc = CRC16_CALC->Process({buf, nb_data_bytes});

    const bool is_match = (crc16_rx == crc16_calc);
    return is_match;
}

void PAD_Data_Group::Reset(void) {
    m_nb_required_bytes = 0;
    m_nb_curr_bytes = 0;
    m_buffer.resize(0);
    m_buffer.clear();
}