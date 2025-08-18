#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <complex>
#include <memory>
#include <vector>
#include "utility/span.h"
#include "ofdm/dab_mapper_ref.h"
#include "ofdm/dab_ofdm_params_ref.h"
#include "ofdm/dab_prs_ref.h"
#include "ofdm/ofdm_demodulator.h"
#include "viterbi_config.h"
#include "./app_io_buffers.h"

class OFDM_Block 
{
private:
    std::shared_ptr<InputBuffer<std::complex<float>>> m_input_stream = nullptr;
    std::shared_ptr<OutputBuffer<viterbi_bit_t>> m_output_stream = nullptr;
    std::unique_ptr<OFDM_Demod> m_ofdm_demod = nullptr;
    std::vector<std::complex<float>> m_buffer;
public:
    OFDM_Block(const int transmission_mode, const size_t total_threads) {
        const auto ofdm_params = get_DAB_OFDM_params(transmission_mode);
        auto ofdm_prs_ref = std::vector<std::complex<float>>(ofdm_params.nb_fft);
        get_DAB_PRS_reference(transmission_mode, ofdm_prs_ref);
        auto ofdm_mapper_ref = std::vector<int>(ofdm_params.nb_data_carriers);
        get_DAB_mapper_ref(ofdm_mapper_ref, ofdm_params.nb_fft);
        m_ofdm_demod = std::make_unique<OFDM_Demod>(ofdm_params, ofdm_prs_ref, ofdm_mapper_ref, int(total_threads));
        m_ofdm_demod->On_OFDM_Frame().Attach([this](tcb::span<const viterbi_bit_t> buf){
            if (m_output_stream == nullptr) return; 
            m_output_stream->write(buf);
        });
    }
    auto& get_ofdm_demod() { return *(m_ofdm_demod.get()); }
    tcb::span<const std::complex<float>> get_buffer() const { return m_buffer; }
    void set_input_stream(std::shared_ptr<InputBuffer<std::complex<float>>> stream) { 
        m_input_stream = stream; 
    }
    void set_output_stream(std::shared_ptr<OutputBuffer<viterbi_bit_t>> stream) { 
        m_output_stream = stream; 
    }
    void run(size_t block_size) {
        if (m_input_stream == nullptr) return;
        m_buffer.resize(block_size);
        bool is_finished = false;
        while (!is_finished) {
            const size_t length = m_input_stream->read(m_buffer);
            if (length != block_size) {
                is_finished = true;
            }
            if (length == 0) break;
            auto buf = tcb::span(m_buffer).first(length);
            m_ofdm_demod->Process(buf);
        }
    }
};
