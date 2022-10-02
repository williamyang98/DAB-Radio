#define _USE_MATH_DEFINES
#include <cmath>

#include "ofdm_demodulator.h"

#include <kiss_fft.h>
#include <cassert>

#include "dab_prs_ref.h"

OFDM_Demodulator::OFDM_Demodulator(
        const struct OFDM_Params _ofdm_params,
        const std::complex<float>* _ofdm_prs_ref)
: params(_ofdm_params)
{
    fft_cfg = kiss_fft_alloc(params.nb_fft ,false , 0, 0);

    total_frames_read = 0;
    total_frames_desync = 0;

    ofdm_magnitude_avg = new float[params.nb_fft];
    for (int i = 0; i < params.nb_fft; i++) {
        ofdm_magnitude_avg[i] = 0.0f;
    }

    // fine frequency adjust
    freq_fine_offset = 0.0f;
    freq_dt = 0.0f;
    is_update_fine_freq = true;

    // ofdm symbol buffers
    ofdm_sym_wrap.buf = new std::complex<float>[params.nb_symbol_period];
    ofdm_sym_wrap.capacity = 0;
    ofdm_sym_pll_buf = new std::complex<float>[params.nb_symbol_period];
    // fft buffers used to processing ofdm symbols
    curr_sym_fft_buf = new std::complex<float>[params.nb_fft];
    last_sym_fft_buf = new std::complex<float>[params.nb_fft];
    // digital data passed to decoders
    ofdm_frame_data = new float[params.nb_data_carriers*params.nb_frame_symbols];
    curr_ofdm_symbol = 0;


    // null symbol buffer
    null_sym_wrap.buf = new std::complex<float>[params.nb_null_period];
    null_sym_wrap.capacity = 0;
    null_sym_pll_buf = new std::complex<float>[params.nb_null_period];
    // fft buffer used to process null symbol
    null_sym_fft_buf = new std::complex<float>[params.nb_fft];
    // digital data passed to decoders
    null_sym_data = new float[params.nb_fft];
    is_read_null_symbol = false;


    // find null sync and first prs symbol 
    is_found_prs = false;
    // null search buffers
    {
        const int N = params.nb_null_period + params.nb_symbol_period;
        null_search.buf = new std::complex<float>[N];
        null_search.length = N;
        null_search.index = 0;
        null_search.prs_index = -1;
        null_search.capacity = 0;
    }
    // linearisation buffer for processing
    // we need a linear buffer to process the PRS (phase reference symbol)
    // worst case, we end up reading the PRS at the very start of the circular search buffer
    // and have alot of excess samples
    {
        const int N = params.nb_null_period + params.nb_symbol_period;
        null_search_prs.buf = new std::complex<float>[N];
        null_search_prs.length = N;
        null_search_prs.capacity = 0;
    }
    // fine time frame synchronisation
    // this uses the reference PRS determined by DAB transmission mode for correlation
    // correlation is done using multiplication in frequency then IFFT
    prs_fft_reference = new std::complex<float>[params.nb_fft];
    prs_fft_actual = new std::complex<float>[params.nb_fft];
    prs_impulse_response = new float[params.nb_fft];
    // method 1: ofdm frame end then correlation
    // method 2: null power detection then correlation
    is_null_start_found = false;
    is_null_end_found = false;
    signal_l1_average = 0.0f;
    signal_l1_beta = 0.9f;
    signal_l1_nb_samples = 50;

    // copy the prs fft reference
    for (int i = 0; i < params.nb_fft; i++) {
        prs_fft_reference[i] = _ofdm_prs_ref[i];
    }

    // initial state 
    state = State::WAITING_NULL;
}

OFDM_Demodulator::~OFDM_Demodulator()
{
    kiss_fft_free(fft_cfg);

    // average spectrum of ofdm symbols
    delete ofdm_magnitude_avg;

    // ofdm symbol buffers
    delete ofdm_sym_wrap.buf;
    delete ofdm_sym_pll_buf;
    delete curr_sym_fft_buf;
    delete last_sym_fft_buf;
    delete ofdm_frame_data;

    // null symbol buffers
    delete null_sym_wrap.buf;
    delete null_sym_pll_buf;
    delete null_sym_data;
    delete null_sym_fft_buf;

    // null search buffers
    delete null_search.buf;
    delete null_search_prs.buf;
    delete prs_fft_reference;
    delete prs_fft_actual;
    delete prs_impulse_response;
}

void OFDM_Demodulator::ProcessBlock(
    std::complex<float>* block, const int N)
{
    UpdateSignalAverage(block, N);
    ProcessBlockWithoutUpdate(block, N);
}


void OFDM_Demodulator::ProcessBlockWithoutUpdate(
    std::complex<float>* block, const int N)
{
    int curr_index = 0;
    while (curr_index < N) {
        auto buf = &block[curr_index];
        const int N_remain = N-curr_index;
        switch (state) {
        case WAITING_NULL:
            {
                const int nb_read = FindNullSync(buf, N_remain);
                curr_index += nb_read;
                if (is_found_prs) {
                    state = State::READING_OFDM_FRAME;
                    curr_ofdm_symbol = 0;
                    freq_dt = 0.0f;
                    ofdm_sym_wrap.capacity = 0; 
                    // backtrack and process partial/whole PRS symbol
                    ProcessBlockWithoutUpdate(null_search_prs.buf, null_search_prs.capacity);
                }
            }
            break;
        case READING_OFDM_FRAME:
            {
                const int nb_read = ReadOFDMSymbols(buf, N_remain);
                curr_index += nb_read;
                if (curr_ofdm_symbol == params.nb_frame_symbols) {
                    state = State::READING_NULL_SYMBOL;
                    total_frames_read++;

                    is_read_null_symbol = false;
                    null_sym_wrap.capacity = 0;
                } 
            }
            break;
        case READING_NULL_SYMBOL:
            {
                const int nb_read = ReadNullSymbol(buf, N_remain);
                curr_index += nb_read;
                if (is_read_null_symbol) {
                    state = State::WAITING_NULL;
                    // reset null search buffer
                    null_search.index = 0;
                    // set the index so we are only required to populate the
                    // circular array then correlate
                    null_search.prs_index = params.nb_null_period;
                    null_search.capacity = 0; 
                    // reset flags
                    is_found_prs = false;
                    is_null_start_found = false;
                    is_null_end_found = false;
                    // backtrack and process null data for null search
                    if (null_sym_wrap.capacity == 0) {
                        ProcessBlockWithoutUpdate(buf, params.nb_null_period);
                    } else {
                        ProcessBlockWithoutUpdate(null_sym_wrap.buf, params.nb_null_period);
                    }
                }
            }
            break;
        }
    }
}

int OFDM_Demodulator::ReadOFDMSymbols(
    std::complex<float>* block, const int N)
{
    const int M = params.nb_symbol_period;

    int curr_index = 0;
    while ((curr_index < N) && (curr_ofdm_symbol < params.nb_frame_symbols)) 
    {
        const int N_remain = N-curr_index;

        // we need wrap around
        if (ofdm_sym_wrap.capacity > 0) {
            const int nb_read = std::min(
                M - ofdm_sym_wrap.capacity,
                N_remain);

            auto* wr_buf = &ofdm_sym_wrap.buf[ofdm_sym_wrap.capacity];
            auto* rd_buf = &block[curr_index];
            for (int i = 0; i < nb_read; i++) {
                wr_buf[i] = rd_buf[i];
            }
            ofdm_sym_wrap.capacity += nb_read;
            curr_index += nb_read;

            // symbol reconstructed, process it 
            if (ofdm_sym_wrap.capacity == M) {
                ProcessOFDMSymbol(ofdm_sym_wrap.buf);
                ofdm_sym_wrap.capacity = 0;
            }
            continue;
        }

        // try to process fft directly
        if (N_remain >= M) {
            auto* rd_buf = &block[curr_index];
            ProcessOFDMSymbol(rd_buf);
            curr_index += M;
            continue;
        }


        // if we have insufficent samples for processing, add to wrap around buffer
        auto* rd_buf = &block[curr_index];
        auto* wr_buf = &ofdm_sym_wrap.buf[ofdm_sym_wrap.capacity];
        for (int i = 0; i < N_remain; i++) {
            wr_buf[i] = rd_buf[i];
        }
        curr_index += N_remain;
        ofdm_sym_wrap.capacity += N_remain;
    }

    return curr_index;
}

void OFDM_Demodulator::ProcessOFDMSymbol(std::complex<float>* sym) 
{
    const float ofdm_freq_spacing = static_cast<float>(params.freq_carrier_spacing);
    const float Ts = 1.0f/2.048e6;
    auto pll_buf = ofdm_sym_pll_buf; 
    auto pll_fft_rd_buf = &pll_buf[params.nb_cyclic_prefix];

    // apply pll
    for (int i = 0; i < params.nb_symbol_period; i++) {
        const auto pll = std::complex<float>(
            std::cosf(freq_dt),
            std::sinf(freq_dt)
        );
        pll_buf[i] = sym[i] * pll;
        freq_dt += 2.0f * (float)M_PI * freq_fine_offset * Ts;
    }

    // calculate fft and get differential qpsk result
    kiss_fft(fft_cfg, 
        (kiss_fft_cpx*)pll_fft_rd_buf, 
        (kiss_fft_cpx*)curr_sym_fft_buf);

    // update the magnitude average
    UpdateMagnitudeAverage(curr_sym_fft_buf);

    // get the dqpsk result we have at least one symbol
    if (curr_ofdm_symbol > 0) {
        const int curr_dqsk_index = curr_ofdm_symbol-1;
        const int M = params.nb_data_carriers/2;
        const int N_fft = params.nb_fft;

        // -N/2 <= x <= -1
        for (int i = 0; i < M; i++) {
            const int j = (N_fft-M+i) % N_fft;
            // arg(z1*~z0) = arg(z1)+arg(~z0) = arg(z1)-arg(z0)
            const auto phase_delta_vec = 
                curr_sym_fft_buf[j] * 
                std::conj(last_sym_fft_buf[j]);
            const float phase_delta = std::atan2f(
                phase_delta_vec.imag(),
                phase_delta_vec.real());

            ofdm_frame_data[curr_dqsk_index*params.nb_data_carriers+i] = phase_delta;
        }

        // 1 <= x <= N/2
        for (int i = 0; i < M; i++) {
            // ignore dc bin in FFt
            const int j = 1+i;

            // arg(z1*~z0) = arg(z1)+arg(~z0) = arg(z1)-arg(z0)
            const auto phase_delta_vec = 
                curr_sym_fft_buf[j] * 
                std::conj(last_sym_fft_buf[j]);
            const float phase_delta = std::atan2f(
                phase_delta_vec.imag(),
                phase_delta_vec.real());

            ofdm_frame_data[curr_dqsk_index*params.nb_data_carriers+i+M] = phase_delta;
        }
    }
    // swap fft buffers
    auto tmp = curr_sym_fft_buf;
    curr_sym_fft_buf = last_sym_fft_buf;
    last_sym_fft_buf = tmp;

    curr_ofdm_symbol++;

    if (!is_update_fine_freq) {
        return;
    }

    // determine the phase error using cyclic prefix
    auto cyclic_prefix_correlation = std::complex<float>(0,0);
    for (int i = 0; i < params.nb_cyclic_prefix; i++) {
        cyclic_prefix_correlation += 
            std::conj(pll_buf[i]) * pll_buf[params.nb_fft+i];
    }

    const float cyclic_error = std::atan2f(
        cyclic_prefix_correlation.imag(), 
        cyclic_prefix_correlation.real());
    
    const float fine_freq_adjust = cyclic_error/(float)M_PI * ofdm_freq_spacing / 2.0f;

    // fine frequency correct is -F/2 <= x <= F/2
    // where F is the carrier spacing
    const float beta = 0.1f;
    freq_fine_offset -= beta*fine_freq_adjust;
    // freq_fine_offset = std::min(std::max(freq_fine_offset, -ofdm_freq_spacing/2.0f), ofdm_freq_spacing/2.0f);
    freq_fine_offset = std::fmodf(
        freq_fine_offset + ofdm_freq_spacing*1.5f, 
        ofdm_freq_spacing); 
    freq_fine_offset -= ofdm_freq_spacing/2.0f;

}

int OFDM_Demodulator::ReadNullSymbol(
    std::complex<float>* block, const int N)
{
    const int M = params.nb_null_period;

    // we need wrap around
    if (null_sym_wrap.capacity > 0) {
        const int nb_read = std::min(
            M - null_sym_wrap.capacity,
            N);

        auto* wr_buf = &null_sym_wrap.buf[null_sym_wrap.capacity];
        auto* rd_buf = block;
        for (int i = 0; i < nb_read; i++) {
            wr_buf[i] = rd_buf[i];
        }
        null_sym_wrap.capacity += nb_read;

        // symbol reconstructed, process it 
        if (null_sym_wrap.capacity == M) {
            ProcessNullSymbol(null_sym_wrap.buf);
            null_sym_wrap.capacity = 0;
        }
        
        return nb_read;
    }

    // try to process fft directly
    if (N >= M) {
        ProcessNullSymbol(block);
        return M;
    }


    // if we have insufficent samples for processing, add to wrap around buffer
    auto* rd_buf = block;
    auto* wr_buf = &null_sym_wrap.buf[null_sym_wrap.capacity];
    for (int i = 0; i < N; i++) {
        wr_buf[i] = rd_buf[i];
    }
    null_sym_wrap.capacity += N;
    return N;
}

void OFDM_Demodulator::ProcessNullSymbol(std::complex<float>* sym) 
{
    const float ofdm_freq_spacing = static_cast<float>(params.freq_carrier_spacing);
    const float Ts = 1.0f/2.048e6;
    auto pll_buf = null_sym_pll_buf; 
    auto pll_fft_rd_buf = &pll_buf[params.nb_null_period-params.nb_fft];

    // apply pll
    float dt = 0.0f;
    for (int i = 0; i < params.nb_null_period; i++) {
        const auto pll = std::complex<float>(
            std::cosf(dt),
            std::sinf(dt)
        );
        pll_buf[i] = sym[i] * pll;
        dt += 2.0f * (float)M_PI * freq_fine_offset * Ts;
    }

    // calculate fft to get TII (transmission identification information)
    kiss_fft(fft_cfg, 
        (kiss_fft_cpx*)pll_fft_rd_buf, 
        (kiss_fft_cpx*)null_sym_fft_buf);
    
    UpdateMagnitudeAverage(null_sym_fft_buf);
    
    for (int i = 0; i < params.nb_fft; i++) {
        const int j = (i + params.nb_fft/2) % params.nb_fft;
        null_sym_data[i] = 20.0f*std::log10(std::abs(null_sym_fft_buf[j]));
    }

    is_read_null_symbol = true;
}

void OFDM_Demodulator::UpdateSignalAverage(
    std::complex<float>* block, const int N)
{
    const int M = N-signal_l1_nb_samples;
    const int K = signal_l1_nb_samples*5;

    for (int i = 0; i < M; i+=K) {
        auto* buf = &block[i];
        const float l1_avg = CalculateL1Average(buf, signal_l1_nb_samples);
        signal_l1_average = 
            (signal_l1_beta)*signal_l1_average +
            (1.0f-signal_l1_beta)*l1_avg;
    }
}

int OFDM_Demodulator::FindNullSync(
    std::complex<float>* block, const int N)
{
    // method 2: null power detection then correlation
    // we run this if we dont have an initial estimate for the prs index
    if (null_search.prs_index == -1) {
        const int M = N-signal_l1_nb_samples;
        const float null_start_thresh = signal_l1_average * 0.35f;
        const float null_end_thresh = signal_l1_average * 0.5f;

        // if the loop doesn't exit then we copy all samples into circular buffer
        int nb_read = N;
        for (int i = 0; i < M; i+=signal_l1_nb_samples) {
            auto* buf = &block[i];
            const float l1_avg = CalculateL1Average(buf, signal_l1_nb_samples);
            if (is_null_start_found) {
                if (l1_avg > null_end_thresh) {
                    is_null_end_found = true;
                    nb_read = i+signal_l1_nb_samples;
                    break;
                }
            } else {
                if (l1_avg < null_start_thresh) {
                    is_null_start_found = true;
                }
            }
        }

        for (int i = 0; i < nb_read; i++) {
            null_search.buf[null_search.index++] = block[i];
            null_search.index = null_search.index % null_search.length;
        }

        // found the end of null, and now we can begin search
        // setup so that null period is at start of circular array
        if (is_null_end_found) {
            null_search.prs_index = null_search.index;
            null_search.capacity = params.nb_null_period;
        }
        return nb_read;
    }

    // if prs index is already set
    // keep reading until we have the null and PRS symbols
    if (null_search.capacity < null_search.length) {
        const int N_remain = null_search.length - null_search.capacity;
        const int nb_read = std::min(N_remain, N);

        for (int i = 0; i < nb_read; i++) {
            null_search.buf[null_search.index++] = block[i];
            null_search.index = null_search.index % null_search.length;
        }
        null_search.capacity += nb_read;
        return nb_read;
    }

    // linearise the PRS estimate and apply PLL
    {
        float dt = 0.0f;
        const float Ts = 1.0f/2.048e6;
        for (int i = 0; i < params.nb_fft; i++) {
            const int j = (null_search.prs_index + i) % null_search.length;
            const auto pll = std::complex<float>(
                std::cosf(dt),
                std::sinf(dt)
            );
            null_search_prs.buf[i] = null_search.buf[j] * pll;
            dt += 2.0f * (float)M_PI * freq_fine_offset * Ts;
        }
    }

    // for the PRS we calculate the impulse response for fine time frame synchronisation
    kiss_fft(fft_cfg, 
        (kiss_fft_cpx*)null_search_prs.buf, 
        (kiss_fft_cpx*)prs_fft_actual);
        
    for (int i = 0; i < params.nb_fft; i++) {
        prs_fft_actual[i] = prs_fft_actual[i] * prs_fft_reference[i];
    }

    kiss_fft(fft_cfg, 
        (kiss_fft_cpx*)prs_fft_actual, 
        (kiss_fft_cpx*)prs_fft_actual);

    for (int i = 0; i < params.nb_fft; i++) {
        const auto& v = prs_fft_actual[i];
        const float A = 20.0f*std::log10(std::abs(v));
        prs_impulse_response[params.nb_fft-i-1] = A;
    }

    // calculate if we have a valid impulse response
    // if the peak is at least X dB above the mean, then we use that as the offset
    float impulse_avg = 0.0f;
    float impulse_max_value = prs_impulse_response[0];
    int impulse_max_index = 0;
    for (int i = 0; i < params.nb_fft; i++) {
        const float v = prs_impulse_response[i];
        impulse_avg += v;
        if (v > impulse_max_value) {
            impulse_max_value = v;
            impulse_max_index = i;
        }
    }
    impulse_avg /= (float)params.nb_fft;

    // threshold required in dB
    const float impulse_peak_threshold = 20.0f;

    // we do not have a valid impulse response
    // this probably means we had a severe desync and should restart
    if ((impulse_max_value - impulse_avg) < impulse_peak_threshold) {
        null_search.index = 0;
        null_search.prs_index = -1;
        null_search.capacity = 0;
        is_found_prs = false;
        is_null_start_found = false;
        is_null_end_found = false;
        total_frames_desync++;
        return 0;
    }

    // otherwise if we had a valid response
    // find the offset we should use into the buffer
    // For an ideal correlation, the peak occurs after the cyclic prefix

    // if the max index is early, then we offset by a negative amount into the circular buffer
    // if the max index is late, then we offset by a positive amount into the circular buffer
    const int offset = impulse_max_index - params.nb_cyclic_prefix;
    const int actual_prs_index = (null_search.prs_index + offset) % null_search.length;
    null_search_prs.capacity = params.nb_symbol_period - offset;

    for (int i = 0; i < null_search_prs.capacity; i++) {
        const int j = (actual_prs_index+i) % null_search.length;
        null_search_prs.buf[i] = null_search.buf[j];
    }
    is_found_prs = true; 

    return 0;
}

float OFDM_Demodulator::CalculateL1Average(
    std::complex<float>* block, const int N)
{
    float l1_avg = 0.0f;
    for (int i = 0; i < N; i++) {
        auto& v = block[i];
        l1_avg += std::abs(v.real()) + std::abs(v.imag());
    }
    l1_avg /= (float)N;
    return l1_avg;
}

void OFDM_Demodulator::UpdateMagnitudeAverage(std::complex<float>* Y)
{
    const float beta = 0.1f;
    for (int i = 0; i < params.nb_fft; i++) {
        auto&v = ofdm_magnitude_avg[i];
        const int j = (i+params.nb_fft/2) % params.nb_fft;

        const float x = 20.0f*std::log10(std::abs(Y[j]));
        v = (1.0f-beta)*v + beta*x;
    }
}