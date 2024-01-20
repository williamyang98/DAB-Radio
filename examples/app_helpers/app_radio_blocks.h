#pragma once

#include <vector>
#include <memory>
#include "basic_radio/basic_radio.h"
#include "dab/constants/dab_parameters.h"
#include "./app_io_buffers.h"

class Basic_Radio_Block
{
private:
    std::shared_ptr<InputBuffer<viterbi_bit_t>> m_input_stream = nullptr;
    std::unique_ptr<BasicRadio> m_basic_radio = nullptr;
    std::vector<viterbi_bit_t> m_bits_buffer;
    DAB_Parameters m_dab_params;
public:
    Basic_Radio_Block(const int transmission_mode, const size_t total_threads)
    {
        m_dab_params = get_dab_parameters(transmission_mode);
        m_basic_radio = std::make_unique<BasicRadio>(m_dab_params, total_threads);
        m_bits_buffer.resize(m_dab_params.nb_frame_bits);
    }
    BasicRadio& get_basic_radio() { return *(m_basic_radio.get()); }
    void set_input_stream(std::shared_ptr<InputBuffer<viterbi_bit_t>> stream) { 
        m_input_stream = stream; 
    }
    void run() {
        if (m_input_stream == nullptr) return;  
        while (true) {
            const size_t length = m_input_stream->read(m_bits_buffer);
            if (length != m_bits_buffer.size()) return;
            m_basic_radio->Process(m_bits_buffer);
        }
    }
};
