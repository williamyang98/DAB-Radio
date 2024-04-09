#include "./ofdm_modulator.h"
#include <stddef.h>
#include <stdint.h>
#include <algorithm>
#include <cmath>
#include <complex>
#include <fftw3.h>
#include "utility/span.h"
#include "./ofdm_params.h"

OFDM_Modulator::OFDM_Modulator(
    const OFDM_Params& params, 
    tcb::span<const std::complex<float>> prs_fft_ref)
:   m_params(params),
    m_frame_out_size(params.nb_null_period + params.nb_symbol_period*params.nb_frame_symbols),
    m_data_in_size((params.nb_frame_symbols-1)*params.nb_data_carriers*2/8)
{
    m_ifft_plan = fftwf_plan_dft_1d((int)m_params.nb_fft, nullptr, nullptr, FFTW_BACKWARD, FFTW_ESTIMATE);

    // interleave the bits for a OFDM symbol containing N data carriers
    m_prs_fft_ref.resize(m_params.nb_fft);
    m_prs_time_ref.resize(m_params.nb_symbol_period);

    std::copy_n(prs_fft_ref.begin(), m_params.nb_fft, m_prs_fft_ref.begin());

    // create our time domain prs symbol with the cyclic prefix
    {
        auto buf = tcb::span(m_prs_time_ref).subspan(m_params.nb_cyclic_prefix, m_params.nb_fft);
        CalculateIFFT(m_prs_fft_ref, buf);
        for (size_t i = 0; i < m_params.nb_cyclic_prefix; i++) {
            m_prs_time_ref[i] = m_prs_time_ref[m_params.nb_fft+i];
        }
    }

    m_last_sym_fft.resize(m_params.nb_fft);
    m_curr_sym_fft.resize(m_params.nb_fft);

    for (size_t i = 0; i < m_params.nb_fft; i++) {
        m_curr_sym_fft[i] = std::complex<float>(0,0);
        m_last_sym_fft[i] = std::complex<float>(0,0);
    }
}

OFDM_Modulator::~OFDM_Modulator() 
{
    fftwf_destroy_plan(m_ifft_plan);
}

bool OFDM_Modulator::ProcessBlock(
    tcb::span<std::complex<float>> frame_out_buf, 
    tcb::span<const uint8_t> data_in_buf)
{
    const size_t nb_frame_out = frame_out_buf.size();
    const size_t nb_data_in = data_in_buf.size();
    // invalid buffer sizes
    if (nb_data_in != m_data_in_size) {
        return false;
    }
    if (nb_frame_out != m_frame_out_size) {
        return false;
    }

    // null period
    for (size_t i = 0; i < m_params.nb_null_period; i++) {
        frame_out_buf[i] = std::complex<float>(0,0);
    }

    // prs symbol
    for (size_t i = 0; i < m_params.nb_symbol_period; i++) {
        frame_out_buf[i+m_params.nb_null_period] = m_prs_time_ref[i];
    }

    // seed the dqpsk fft buffers
    for (size_t i = 0; i < m_params.nb_fft; i++) {
        m_last_sym_fft[i] = m_prs_fft_ref[i];
    }

    // data symbols
    {
        const size_t nb_data_symbols = m_params.nb_frame_symbols-1;
        const size_t nb_sym_data_in = m_params.nb_data_carriers*2/8; 
        const size_t nb_sym_out = m_params.nb_symbol_period;
        // account for the PRS (phase reference symbol)
        auto data_symbols = frame_out_buf.subspan(m_params.nb_null_period + m_params.nb_symbol_period);
        for (size_t i = 0; i < nb_data_symbols; i++) {
            CreateDataSymbol(
                data_in_buf.subspan(i*nb_sym_data_in, nb_sym_data_in),
                data_symbols.subspan(i*nb_sym_out, nb_sym_out));
        }
    }

    return true;
}

void OFDM_Modulator::CreateDataSymbol(
    tcb::span<const uint8_t> sym_data_in, 
    tcb::span<std::complex<float>> sym_out)
{
    const size_t nb_data_in = sym_data_in.size(); 

    const float A = 1.0f/std::sqrt(2.0f);
    const std::complex<float> PHASE_MAP[4] = {{-A,-A}, {A,-A}, {A,A}, {-A,A}};

    // Create raw fft bins
    {
        // create fft for -F/2 <= f < 0
        size_t curr_carrier = m_params.nb_fft - m_params.nb_data_carriers/2;
        for (size_t i = 0; i < nb_data_in/2; i++) {
            const uint8_t b = sym_data_in[i];
            m_curr_sym_fft[curr_carrier++] = PHASE_MAP[((b >> 0) & 0b11)];
            m_curr_sym_fft[curr_carrier++] = PHASE_MAP[((b >> 2) & 0b11)];
            m_curr_sym_fft[curr_carrier++] = PHASE_MAP[((b >> 4) & 0b11)];
            m_curr_sym_fft[curr_carrier++] = PHASE_MAP[((b >> 6) & 0b11)];
        }

        // create fft for 0 < f <= F/2 
        curr_carrier = 1;
        for (size_t i = 0; i < nb_data_in/2; i++) {
            const size_t j = nb_data_in/2 + i;
            const uint8_t b = sym_data_in[j];
            m_curr_sym_fft[curr_carrier++] = PHASE_MAP[((b >> 0) & 0b11)];
            m_curr_sym_fft[curr_carrier++] = PHASE_MAP[((b >> 2) & 0b11)];
            m_curr_sym_fft[curr_carrier++] = PHASE_MAP[((b >> 4) & 0b11)];
            m_curr_sym_fft[curr_carrier++] = PHASE_MAP[((b >> 6) & 0b11)];
        }
    }


    // get the dqpsk
    // arg(z0*z1) = arg(z0) + arg(z1)
    {
        for (size_t i = 0; i < m_params.nb_data_carriers/2; i++) {
            const size_t j = m_params.nb_fft - m_params.nb_data_carriers/2 + i;
            m_curr_sym_fft[j] = m_last_sym_fft[j] * m_curr_sym_fft[j];
        }

        for (size_t i = 0; i < m_params.nb_data_carriers/2; i++) {
            const size_t j = 1+i;
            m_curr_sym_fft[j] = m_last_sym_fft[j] * m_curr_sym_fft[j];
        }
    }

    // get ifft of symbol
    {
        auto buf = sym_out.subspan(m_params.nb_cyclic_prefix, m_params.nb_fft);
        CalculateIFFT(m_curr_sym_fft, buf);
    }

    // create cyclic prefix
    for (size_t i = 0; i < m_params.nb_cyclic_prefix; i++) {
        sym_out[i] = sym_out[i+m_params.nb_fft];
    }

    // swap fft buffers
    std::swap(m_last_sym_fft, m_curr_sym_fft);
}

void OFDM_Modulator::CalculateIFFT(
    tcb::span<const std::complex<float>> fft_in, 
    tcb::span<std::complex<float>> fft_out)
{
    fftwf_execute_dft(
        m_ifft_plan, 
        (fftwf_complex*)fft_in.data(),
        (fftwf_complex*)fft_out.data());
}