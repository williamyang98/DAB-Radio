#define _USE_MATH_DEFINES
#include <cmath>

#include "ofdm_demodulator.h"

#include <kiss_fft.h>
#include <cassert>

#include "dab_prs_ref.h"

static constexpr float Fs = 2.048e6;
static constexpr float Ts = 1.0f/Fs;

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
    ofdm_sym_wrap_buf = new ReconstructionBuffer<std::complex<float>>(params.nb_symbol_period);
    ofdm_sym_pll_buf = new std::complex<float>[params.nb_symbol_period];
    // fft buffers used to processing ofdm symbols
    curr_sym_fft_buf = new std::complex<float>[params.nb_fft];
    last_sym_fft_buf = new std::complex<float>[params.nb_fft];
    // digital data passed to decoders
    {
        // we have one less symbol due to the differential encoding between symbols
        const int N = params.nb_data_carriers*(params.nb_frame_symbols-1);
        ofdm_frame_raw = new float[N];
        ofdm_frame_pred = new uint8_t[N];
    }
    curr_ofdm_symbol = 0;


    // null symbol buffer
    null_sym_wrap_buf = new ReconstructionBuffer<std::complex<float>>(params.nb_null_period);
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
        null_search_buf = new CircularBuffer<std::complex<float>>(N);
        null_search_prs_index = -1;
    }
    // linearisation buffer for processing
    // we need a linear buffer to process the PRS (phase reference symbol)
    // worst case, we end up reading the PRS at the very start of the circular search buffer
    // and have alot of excess samples
    {
        const int N = params.nb_null_period + params.nb_symbol_period;
        null_prs_linearise_buf = new ReconstructionBuffer<std::complex<float>>(N);
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

    // copy the prs fft reference
    // when we are doing our time domain correlation by multiplying in frequency domain
    // we need to multiply it by the conjugate
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
        case READING_OFDM_FRAME:
            {
                const int nb_read = ReadOFDMSymbols(buf, N_remain);
                curr_index += nb_read;
                if (curr_ofdm_symbol == params.nb_frame_symbols) {
                    if (callback) {
                        // we ignore the PRS since that is just a reference
                        // differential encoding means we have one less symbol that contains data
                        callback->OnOFDMFrame(ofdm_frame_pred, params.nb_data_carriers, params.nb_frame_symbols-1);
                    }
                    state = State::READING_NULL_SYMBOL;
                    total_frames_read++;

                    is_read_null_symbol = false;
                    null_sym_wrap_buf->Reset();
                    null_prs_linearise_buf->Reset();
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

int OFDM_Demodulator::ReadOFDMSymbols(
    std::complex<float>* block, const int N)
{
    const int M = params.nb_symbol_period;
    // we need wrap around
    if (!ofdm_sym_wrap_buf->IsEmpty() || (N < M)) {
        const int nb_read = ofdm_sym_wrap_buf->ConsumeBuffer(block, N);

        if (ofdm_sym_wrap_buf->IsFull()) {
            ProcessOFDMSymbol(ofdm_sym_wrap_buf->GetData());
            ofdm_sym_wrap_buf->Reset();
        }
        return nb_read;
    }

    // we have enough samples to read the whole symbol
    ProcessOFDMSymbol(block);
    return M;
}

void OFDM_Demodulator::ProcessOFDMSymbol(std::complex<float>* sym) 
{
    const float ofdm_freq_spacing = static_cast<float>(params.freq_carrier_spacing);
    auto pll_buf = ofdm_sym_pll_buf; 
    auto pll_fft_rd_buf = &pll_buf[params.nb_cyclic_prefix];

    // apply pll
    freq_dt = ApplyPLL(sym, pll_buf, params.nb_symbol_period, freq_dt);

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

        const int offset = curr_dqsk_index*params.nb_data_carriers;
        auto* ofdm_raw = &ofdm_frame_raw[offset];
        auto* ofdm_pred = &ofdm_frame_pred[offset];

        // conversion of phase-delta to value between 0 to 4
        static auto map_phase = [](float x) {
            // y = (x*4/pi + 3) / 2
            // y = x*2/pi + 1.5
            const float y = x*2.0f/(float)M_PI + 1.5f;
            return static_cast<uint8_t>(std::round(y) + 4.0f) % 4;
        };

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

            ofdm_raw[i] = phase_delta;
            ofdm_pred[i] = map_phase(phase_delta);
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
            ofdm_raw[i+M] = phase_delta;
            ofdm_pred[i+M] = map_phase(phase_delta);
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
    freq_fine_offset -= cfg.fine_freq_update_beta*fine_freq_adjust;
    // floating point modulus to keep between -F/2 <= x <= F/2
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
    if (!null_sym_wrap_buf->IsEmpty() || (N < M)) {
        const int nb_read = null_sym_wrap_buf->ConsumeBuffer(block, N);
        if (null_sym_wrap_buf->IsFull()) {
            ProcessNullSymbol(null_sym_wrap_buf->GetData());
            null_sym_wrap_buf->Reset();
        }
        return nb_read;
    }

    // we have enough samples to read the whole symbol
    ProcessNullSymbol(block);
    return M;
}

void OFDM_Demodulator::ProcessNullSymbol(std::complex<float>* sym) 
{
    const float ofdm_freq_spacing = static_cast<float>(params.freq_carrier_spacing);
    auto* rd_buf = &sym[params.nb_null_period-params.nb_fft];

    // apply pll
    ApplyPLL(rd_buf, null_sym_pll_buf, params.nb_fft);

    // calculate fft to get TII (transmission identification information)
    kiss_fft(fft_cfg, 
        (kiss_fft_cpx*)null_sym_pll_buf, 
        (kiss_fft_cpx*)null_sym_fft_buf);

    // NOTE:Ignore the magnitude contribution of the NULL symbol
    // This is not representative of the data symbol spectrum    
    // UpdateMagnitudeAverage(null_sym_fft_buf);
    
    for (int i = 0; i < params.nb_fft; i++) {
        const int j = (i + params.nb_fft/2) % params.nb_fft;
        null_sym_data[i] = 20.0f*std::log10(std::abs(null_sym_fft_buf[j]));
    }

    is_read_null_symbol = true;
}

int OFDM_Demodulator::FindNullSync(
    std::complex<float>* block, const int N)
{
    // method 2: null power detection then correlation
    // we run this if we dont have an initial estimate for the prs index
    if (null_search_prs_index == -1) {
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
        // found the end of null, and now we can begin search
        // setup so that null period is at start of circular array
        if (is_null_end_found) {
            null_search_prs_index = null_search_buf->GetIndex();
            null_search_buf->SetLength(params.nb_null_period);
        }
        return nb_read;
    }

    // if prs index is already set
    // keep reading until we have the null and PRS symbols
    if (!null_search_buf->IsFull()) {
        const int nb_read = null_search_buf->ConsumeBuffer(block, N);
        return nb_read;
    }

    // linearise the PRS estimate and apply PLL
    {
        for (int i = 0; i < params.nb_fft; i++) {
            const int j = null_search_prs_index+i;
            null_prs_linearise_buf->At(i) = null_search_buf->At(j);
        }
        auto buf = null_prs_linearise_buf->GetData();
        ApplyPLL(buf, buf, params.nb_fft);
    }

    // for the PRS we calculate the impulse response for fine time frame synchronisation
    kiss_fft(fft_cfg, 
        (kiss_fft_cpx*)(null_prs_linearise_buf->GetData()), 
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
        // NOTE: flip the buffer since we are using fft for ifft
        // this causes the time domain result to be reversed
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

    // we do not have a valid impulse response
    // this probably means we had a severe desync and should restart
    if ((impulse_max_value - impulse_avg) < cfg.impulse_peak_threshold_db) {
        null_search_prs_index = -1;
        null_search_buf->Reset();
        null_prs_linearise_buf->Reset();
        is_found_prs = false;
        is_null_start_found = false;
        is_null_end_found = false;
        total_frames_desync++;
        // additionally if we have completely lost synchronisation
        // we need to reset the fine tune frequency offset since that could be 
        // this is because if it is extremely wrong, our correlation will be poor
        freq_fine_offset = 0.0f;
        signal_l1_average = 0.0f;
        return 0;
    }

    // otherwise if we had a valid response
    // find the offset we should use into the buffer
    // For an ideal correlation, the peak occurs after the cyclic prefix

    // if the max index is early, then we offset by a negative amount into the circular buffer
    // if the max index is late, then we offset by a positive amount into the circular buffer
    const int offset = impulse_max_index - params.nb_cyclic_prefix;

    // NOTE: We add the capacity as well onto the actual_prs_index
    // This is because the offset can cause the resulting adjusted index to be negative
    const int actual_prs_index = (null_search_prs_index + offset + null_search_buf->Capacity());
    null_prs_linearise_buf->SetLength(params.nb_symbol_period - offset);

    for (int i = 0; i < null_prs_linearise_buf->Length(); i++) {
        const int j = actual_prs_index+i;
        null_prs_linearise_buf->At(i) = null_search_buf->At(j);
    }
    
    is_found_prs = true; 

    return 0;
}

void OFDM_Demodulator::UpdateSignalAverage(
    std::complex<float>* block, const int N)
{
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
    const float beta = cfg.data_sym_magnitude_update_beta;
    for (int i = 0; i < params.nb_fft; i++) {
        auto&v = ofdm_magnitude_avg[i];
        const int j = (i+params.nb_fft/2) % params.nb_fft;

        const float x = 20.0f*std::log10(std::abs(Y[j]));
        v = (1.0f-beta)*v + beta*x;
    }
}

float OFDM_Demodulator::ApplyPLL(
    const std::complex<float>* x, std::complex<float>* y, 
    const int N, const float dt0)
{
    float dt = dt0;
    for (int i = 0; i < N; i++) {
        const auto pll = std::complex<float>(
            std::cosf(dt),
            std::sinf(dt));
        y[i] = x[i] * pll;
        dt += 2.0f * (float)M_PI * freq_fine_offset * Ts;
    }
    return dt;
}