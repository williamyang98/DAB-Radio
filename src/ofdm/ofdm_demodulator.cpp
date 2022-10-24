// DOC: docs/DAB_implementation_in_SDR_detailed.pdf
// Referring to clause 3.10 - Reception side
//        up to clause 3.15 - Differential demodulator
// These clauses provide extremely detailed explanations of how to synchronise and interpret the OFDM frame


#define _USE_MATH_DEFINES
#include <cmath>
#include <complex>

#include "ofdm_demodulator.h"
#include <kiss_fft.h>
#include <cassert>

static constexpr float Fs = 2.048e6;
static constexpr float Ts = 1.0f/Fs;

#define USE_PLL_TABLE 1

#if USE_PLL_TABLE
#include "quantized_oscillator.h"
// Frequency resolution of table can't be too small otherwise we end up with a large table
// This could cause inefficient memory access and cache misses
static auto PLL_TABLE = QuantizedOscillator(5, static_cast<int>(Fs));
#endif

// argument of complex number using floating points
// std::arg promotes the real and imaginary components to float which introduces a performance penalty
static inline float cargf(const std::complex<float>& x) {
    return std::atan2f(x.imag(), x.real());
}

// conversion of phase-delta to value between 0 to 4
//  0 = -3pi/4 
//  1 =  -pi/4 
//  2 =   pi/4 
//  3 =  3pi/4
static uint8_t map_phase(float x) {
    // y = (x*4/pi + 3) / 2
    // y = x*2/pi + 1.5
    const float y = x*2.0f/(float)M_PI + 1.5f;
    return static_cast<uint8_t>(std::round(y) + 4.0f) % 4;
};


OFDM_Demodulator::OFDM_Demodulator(
        const struct OFDM_Params _ofdm_params,
        const std::complex<float>* _ofdm_prs_ref)
: params(_ofdm_params)
{
    fft_cfg = kiss_fft_alloc(params.nb_fft ,false , 0, 0);

    total_frames_read = 0;
    total_frames_desync = 0;


    // Fine frequency synchronisation variables
    freq_fine_offset = 0.0f;
    freq_dt = 0.0f;
    is_update_fine_freq = true;

    // Process OFDM symbols to get subcarrier data
    curr_ofdm_symbol = 0;
    ofdm_sym_wrap_buf = new ReconstructionBuffer<std::complex<float>>(params.nb_symbol_period);
    ofdm_sym_pll_buf = new std::complex<float>[params.nb_symbol_period];
    curr_sym_fft_buf = new std::complex<float>[params.nb_fft];
    last_sym_fft_buf = new std::complex<float>[params.nb_fft];
    {
        // we have one less symbol due to the differential encoding between symbols
        const int N = params.nb_data_carriers*(params.nb_frame_symbols-1);
        ofdm_frame_raw = new float[N];
        ofdm_frame_pred = new uint8_t[N];
    }
    ofdm_magnitude_avg = new float[params.nb_fft];
    for (int i = 0; i < params.nb_fft; i++) {
        ofdm_magnitude_avg[i] = 0.0f;
    }

    // Process null symbol to get TII information from spectrum
    null_sym_wrap_buf = new ReconstructionBuffer<std::complex<float>>(params.nb_null_period);
    null_sym_pll_buf = new std::complex<float>[params.nb_null_period];
    null_sym_fft_buf = new std::complex<float>[params.nb_fft];
    null_sym_data = new float[params.nb_fft];
    is_read_null_symbol = false;

    // Fine time synchronisation using power detection
    is_found_prs = false;
    {
        const int N = params.nb_null_period + params.nb_symbol_period;
        null_search_buf = new CircularBuffer<std::complex<float>>(N);
        null_search_prs_index = -1;
    }
    {
        const int N = params.nb_null_period + params.nb_symbol_period;
        null_prs_linearise_buf = new ReconstructionBuffer<std::complex<float>>(N);
    }

    // Fine time synchronisation using PRS correlation
    prs_fft_reference = new std::complex<float>[params.nb_fft];
    prs_fft_actual = new std::complex<float>[params.nb_fft];
    prs_impulse_response = new float[params.nb_fft];
    is_null_start_found = false;
    is_null_end_found = false;
    signal_l1_average = 0.0f;

    // PRS correlation in time domain is the conjugate product in frequency domain
    for (int i = 0; i < params.nb_fft; i++) {
        prs_fft_reference[i] = std::conj(_ofdm_prs_ref[i]);
    }

    // initial state 
    state = State::WAITING_NULL;
}

OFDM_Demodulator::~OFDM_Demodulator()
{
    kiss_fft_free(fft_cfg);

    // average spectrum of ofdm symbols
    delete [] ofdm_magnitude_avg;

    // ofdm symbol buffers
    delete ofdm_sym_wrap_buf;
    delete [] ofdm_sym_pll_buf;
    delete [] curr_sym_fft_buf;
    delete [] last_sym_fft_buf;
    delete [] ofdm_frame_raw;
    delete [] ofdm_frame_pred;

    // null symbol buffers
    delete null_sym_wrap_buf;
    delete [] null_sym_pll_buf;
    delete [] null_sym_data;
    delete [] null_sym_fft_buf;

    // null search buffers
    delete null_search_buf;
    delete null_prs_linearise_buf;
    delete [] prs_fft_reference;
    delete [] prs_fft_actual;
    delete [] prs_impulse_response;
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

        // DOC: docs/DAB_implementation_in_SDR_detailed.pdf
        // Clause 3.12: Timing synchronisation
        // Clause 3.12.1: Symbol timing synchronisation
        // Clause 3.12.2: Frame synchronisation
        case WAITING_NULL:
            {
                const int nb_read = FindNullSync(buf, N_remain);
                curr_index += nb_read;
                if (is_found_prs) {
                    state = State::READING_OFDM_FRAME;
                    curr_ofdm_symbol = 0;
                    freq_dt = 0.0f;
                    ofdm_sym_wrap_buf->Reset();
                    // backtrack and process partial/whole PRS symbol
                    ProcessBlockWithoutUpdate(
                        null_prs_linearise_buf->GetData(),
                        null_prs_linearise_buf->Length());
                }
            }
            break;

        // DOC: docs/DAB_implementation_in_SDR_detailed.pdf
        // Clause 3.13: Frequency offset estimation and correction
        // Clause 3.13.1: Fractional frequency offset estimation
        // TODO: Clause 3.13.2: Integral frequency offset estimation (not implemented yet)
        // Clause 3.14: OFDM symbol demodulator
        // Clause 3.14.1: Cyclic prefix removal
        // Clause 3.14.2: FFT
        // Clause 3.14.3: Zero padding removal from FFT (Only include the carriers that are associated with this OFDM transmitter)
        // Clause 3.15: Differential demodulator
        // NOTE: Clause 3.16: Data demapper is done in ofdm_symbol_mapper.h
        case READING_OFDM_FRAME:
            {
                const int nb_read = ReadOFDMSymbols(buf, N_remain);
                curr_index += nb_read;
                if (curr_ofdm_symbol == params.nb_frame_symbols) {
                    // differential encoding means dqsk data symbols is one less than total symbols
                    obs_on_ofdm_frame.Notify(ofdm_frame_pred, params.nb_data_carriers, params.nb_frame_symbols-1);
                    state = State::READING_NULL_SYMBOL;
                    total_frames_read++;

                    is_read_null_symbol = false;
                    null_sym_wrap_buf->Reset();
                    null_prs_linearise_buf->Reset();
                } 
            }
            break;

        // DOC: ETSI EN 300 401
        // Clause 14.8 Transmitter Identification Information signal 
        // TODO: The NULL symbol contains the TII signal. 
        // We can decode this to get transmitter information
        case READING_NULL_SYMBOL:
            {
                const int nb_read = ReadNullSymbol(buf, N_remain);
                curr_index += nb_read;
                if (is_read_null_symbol) {
                    state = State::WAITING_NULL;
                    // reset null search buffer
                    null_search_buf->Reset();
                    null_search_prs_index = params.nb_null_period;
                    null_prs_linearise_buf->Reset();
                    // reset flags
                    is_found_prs = false;
                    is_null_start_found = false;
                    is_null_end_found = false;
                    // backtrack and process null data for null search
                    if (null_sym_wrap_buf->IsEmpty()) {
                        ProcessBlockWithoutUpdate(buf, params.nb_null_period);
                    } else {
                        ProcessBlockWithoutUpdate(
                            null_sym_wrap_buf->GetData(), 
                            null_sym_wrap_buf->Length());
                    }
                }
            }
            break;
        }
    }
}

int OFDM_Demodulator::ReadOFDMSymbols(std::complex<float>* block, const int N) {
    const int M = params.nb_symbol_period;
    // If we need to build up the symbol 
    if (!ofdm_sym_wrap_buf->IsEmpty() || (N < M)) {
        const int nb_read = ofdm_sym_wrap_buf->ConsumeBuffer(block, N);
        if (ofdm_sym_wrap_buf->IsFull()) {
            ProcessOFDMSymbol(ofdm_sym_wrap_buf->GetData());
            ofdm_sym_wrap_buf->Reset();
        }
        return nb_read;
    }

    ProcessOFDMSymbol(block);
    return M;
}

int OFDM_Demodulator::ReadNullSymbol(std::complex<float>* block, const int N) {
    const int M = params.nb_null_period;
    // If we need to build up the null symbol 
    if (!null_sym_wrap_buf->IsEmpty() || (N < M)) {
        const int nb_read = null_sym_wrap_buf->ConsumeBuffer(block, N);
        if (null_sym_wrap_buf->IsFull()) {
            ProcessNullSymbol(null_sym_wrap_buf->GetData());
            null_sym_wrap_buf->Reset();
        }
        return nb_read;
    }

    ProcessNullSymbol(block);
    return M;
}

void OFDM_Demodulator::ProcessOFDMSymbol(std::complex<float>* sym) {
    const float ofdm_freq_spacing = static_cast<float>(params.freq_carrier_spacing);
    auto pll_buf = ofdm_sym_pll_buf; 

    // Clause 3.14.1 - Cyclic prefix removal
    auto pll_fft_rd_buf = &pll_buf[params.nb_cyclic_prefix];

    // Clause 3.14.2 - FFT
    // calculate fft and get differential qpsk result
    freq_dt = ApplyPLL(sym, pll_buf, params.nb_symbol_period, freq_dt);
    kiss_fft(fft_cfg, 
        (kiss_fft_cpx*)pll_fft_rd_buf, 
        (kiss_fft_cpx*)curr_sym_fft_buf);
    UpdateMagnitudeAverage(curr_sym_fft_buf);

    // Clause 3.15 - Differential demodulator
    // get the dqpsk result we have at least one symbol
    if (curr_ofdm_symbol > 0) {
        const int curr_dqsk_index = curr_ofdm_symbol-1;
        const int M = params.nb_data_carriers/2;
        const int N_fft = params.nb_fft;

        const int offset = curr_dqsk_index*params.nb_data_carriers;
        auto* ofdm_raw = &ofdm_frame_raw[offset];
        auto* ofdm_pred = &ofdm_frame_pred[offset];

        // Clause 3.14.3 - Zero padding removal
        // We store the subcarriers that carry information
        for (int i = -M, subcarrier_index = 0; i <= M; i++) {
            // The DC bin carries no information
            if (i == 0) {
                continue;
            }
            const int fft_index = (N_fft+i) % N_fft;

            // arg(z1*~z0) = arg(z1)+arg(~z0) = arg(z1)-arg(z0)
            const auto phase_delta_vec = 
                curr_sym_fft_buf[fft_index] * 
                std::conj(last_sym_fft_buf[fft_index]);

            const float phase_delta = cargf(phase_delta_vec);
            ofdm_raw[subcarrier_index] = phase_delta;
            ofdm_pred[subcarrier_index] = map_phase(phase_delta);
            subcarrier_index++;
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

    // Clause 3.13.1 - Fraction frequency offset estimation
    // determine the phase error using cyclic prefix
    const float cyclic_error = CalculateCyclicPhaseError(pll_buf);
    const float fine_freq_adjust = cyclic_error/(float)M_PI * ofdm_freq_spacing/2.0f;
    freq_fine_offset -= cfg.fine_freq_update_beta*fine_freq_adjust;
    freq_fine_offset = std::fmodf(freq_fine_offset, ofdm_freq_spacing/2.0f);
}

void OFDM_Demodulator::ProcessNullSymbol(std::complex<float>* sym) {
    const float ofdm_freq_spacing = static_cast<float>(params.freq_carrier_spacing);
    auto* rd_buf = &sym[params.nb_null_period-params.nb_fft];
    is_read_null_symbol = true;

    if (!cfg.toggle_flags.is_update_tii_sym_mag) {
        return;
    }

    // Get TII (transmission identification information)
    ApplyPLL(rd_buf, null_sym_pll_buf, params.nb_fft);
    kiss_fft(fft_cfg, 
        (kiss_fft_cpx*)null_sym_pll_buf, 
        (kiss_fft_cpx*)null_sym_fft_buf);
    for (int i = 0; i < params.nb_fft; i++) {
        const int j = (i + params.nb_fft/2) % params.nb_fft;
        null_sym_data[i] = 20.0f*std::log10(std::abs(null_sym_fft_buf[j]));
    }
}

int OFDM_Demodulator::FindNullSync(std::complex<float>* block, const int N) {
    if (null_search_prs_index == -1) {
        return FindNullSync_Power(block, N);
    } else {
        return FindNullSync_Correlation(block, N);
    }
}

int OFDM_Demodulator::FindNullSync_Power(std::complex<float>* block, const int N) {
    // Clause 3.12.2 - Frame synchronisation using power detection
    // we run this if we dont have an initial estimate for the prs index
    // This can occur if:
    //      1. We just started the demodulator and need a quick estimate of OFDM start
    //      2. The PRS impulse response didn't have a sufficiently large peak

    // We analyse the average power of the signal using blocks of size K
    const int K = cfg.signal_l1.nb_samples;
    const int M = N-K;

    const float null_start_thresh = signal_l1_average * cfg.null_l1_search.thresh_null_start;
    const float null_end_thresh = signal_l1_average * cfg.null_l1_search.thresh_null_end;

    // if the loop doesn't exit then we copy all samples into circular buffer
    int nb_read = N;
    for (int i = 0; i < M; i+=K) {
        auto* buf = &block[i];
        const float l1_avg = CalculateL1Average(buf, K);
        if (is_null_start_found) {
            if (l1_avg > null_end_thresh) {
                is_null_end_found = true;
                nb_read = i+K;
                break;
            }
        } else {
            if (l1_avg < null_start_thresh) {
                is_null_start_found = true;
            }
        }
    }

    null_search_buf->ConsumeBuffer(block, nb_read, true);
    if (is_null_end_found) {
        // Remember the index into the circular buffer where the PRS starts
        null_search_prs_index = null_search_buf->GetIndex();
        // Adjust the head of the circular buffer so it points to the start of the NULL symbol
        null_search_buf->SetLength(params.nb_null_period);
    }
    return nb_read;
}

int OFDM_Demodulator::FindNullSync_Correlation(std::complex<float>* block, const int N) {
    // Clause 3.12.1 - Symbol timing synchronisation
    // To synchronise to start of the PRS we calculate the impulse response 

    // Get a window of samples that should encompass the null and PRS symbol
    // This is done so that even if the frame is horribly desynced
    // we should still be able to have enough of the PRS to do time domain correlation
    if (!null_search_buf->IsFull()) {
        const int nb_read = null_search_buf->ConsumeBuffer(block, N);
        return nb_read;
    }

    // Move the circular buffer into a flat array for the FFT
    {
        for (int i = 0; i < params.nb_fft; i++) {
            const int j = null_search_prs_index+i;
            null_prs_linearise_buf->At(i) = null_search_buf->At(j);
        }
        auto buf = null_prs_linearise_buf->GetData();
        ApplyPLL(buf, buf, params.nb_fft);
    }

    // Correlation in time domain is done by doing multiplication in frequency domain
    auto* prs_fft_estimate = null_prs_linearise_buf->GetData();
    kiss_fft(fft_cfg, (kiss_fft_cpx*)prs_fft_estimate, (kiss_fft_cpx*)prs_fft_actual);
    for (int i = 0; i < params.nb_fft; i++) {
        prs_fft_actual[i] = prs_fft_actual[i] * prs_fft_reference[i];
    }

    // Get IFFT to get our correlation result
    // NOTE: The result is actually flipped in time domain, so we unflip it
    kiss_fft(fft_cfg, (kiss_fft_cpx*)prs_fft_actual, (kiss_fft_cpx*)prs_fft_actual);
    for (int i = 0; i < params.nb_fft; i++) {
        const auto& v = prs_fft_actual[i];
        const float A = 20.0f*std::log10(std::abs(v));
        prs_impulse_response[params.nb_fft-i-1] = A;
    }

    // calculate if we have a valid impulse response
    // if the peak is at least X dB above the mean, then we use that as our PRS starting index
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

    // If the main lobe is insufficiently powerful we do not have a valid impulse response
    // This probably means we had a severe desync and should restart
    if ((impulse_max_value - impulse_avg) < cfg.impulse_peak_threshold_db) {
        null_search_prs_index = -1;
        null_search_buf->Reset();
        null_prs_linearise_buf->Reset();
        is_found_prs = false;
        is_null_start_found = false;
        is_null_end_found = false;
        total_frames_desync++;

        // NOTE: We also reset fine frequency synchronisation since an incorrect value
        // can reduce performance of fine time synchronisation using the impulse response
        freq_fine_offset = 0.0f;
        signal_l1_average = 0.0f;
        return 0;
    }

    // The PRS correlation lobe occurs just after the cyclic prefix
    // We actually want the index at the start of the cyclic prefix, so we adjust offset for that
    const int offset = impulse_max_index - params.nb_cyclic_prefix;
    const int actual_prs_index = (null_search_prs_index + offset + null_search_buf->Capacity());

    const int available_prs_length = params.nb_symbol_period - offset;
    null_prs_linearise_buf->SetLength(available_prs_length);
    for (int i = 0; i < available_prs_length; i++) {
        const int j = actual_prs_index+i;
        null_prs_linearise_buf->At(i) = null_search_buf->At(j);
    }
    is_found_prs = true; 
    return 0;
}

float OFDM_Demodulator::CalculateL1Average(std::complex<float>* block, const int N) {
    float l1_avg = 0.0f;
    for (int i = 0; i < N; i++) {
        auto& v = block[i];
        l1_avg += std::abs(v.real()) + std::abs(v.imag());
    }
    l1_avg /= (float)N;
    return l1_avg;
}

float OFDM_Demodulator::CalculateCyclicPhaseError(const std::complex<float>* sym) {
    auto error_vec = std::complex<float>(0,0);
    for (int i = 0; i < params.nb_cyclic_prefix; i++) {
        error_vec += std::conj(sym[i]) * sym[params.nb_fft+i];
    }
    return cargf(error_vec);
}

void OFDM_Demodulator::UpdateSignalAverage(std::complex<float>* block, const int N) {
    const int K = cfg.signal_l1.nb_samples;
    const int M = N-K;
    const int L = K*cfg.signal_l1.nb_decimate;
    const float beta = cfg.signal_l1.update_beta;

    for (int i = 0; i < M; i+=L) {
        auto* buf = &block[i];
        const float l1_avg = CalculateL1Average(buf, K);
        signal_l1_average = 
            (beta)*signal_l1_average +
            (1.0f-beta)*l1_avg;
    }
}


void OFDM_Demodulator::UpdateMagnitudeAverage(std::complex<float>* Y) {
    if (!cfg.toggle_flags.is_update_data_sym_mag) {
        return;
    }

    const float beta = cfg.data_sym_magnitude_update_beta;
    for (int i = 0; i < params.nb_fft; i++) {
        auto&v = ofdm_magnitude_avg[i];
        const int j = (i+params.nb_fft/2) % params.nb_fft;

        const float x = 20.0f*std::log10(std::abs(Y[j]));
        v = (1.0f-beta)*v + beta*x;
    }
}

// Given the fine frequency offset, compensate by multiplying with
// a complex local oscillator with the opposite frequency
float OFDM_Demodulator::ApplyPLL(
    const std::complex<float>* x, std::complex<float>* y, 
    const int N, const float dt0)
{
    #if !USE_PLL_TABLE

    float dt = dt0;
    for (int i = 0; i < N; i++) {
        const auto pll = std::complex<float>(
            std::cosf(dt),
            std::sinf(dt));
        y[i] = x[i] * pll;
        dt += 2.0f * (float)M_PI * freq_fine_offset * Ts;
    }
    return dt;

    #else

    const int K = PLL_TABLE.GetFrequencyResolution();
    const int M = PLL_TABLE.GetTableSize();
    const int step = static_cast<int>(freq_fine_offset) / K;

    int dt = static_cast<int>(dt0);
    for (int i = 0; i < N; i++) {
        y[i] = x[i] * PLL_TABLE.At(static_cast<int>(dt));
        dt = (dt + step + M) % M;
    }
    return static_cast<float>(dt);

    #endif
}