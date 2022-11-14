#define _USE_MATH_DEFINES
#include <cmath>

#include "ofdm_demodulator.h"
#include "ofdm_demodulator_threads.h"
#include <kiss_fft.h>

static constexpr float Fs = 2.048e6;
static constexpr float Ts = 1.0f/Fs;

#define USE_PLL_TABLE 1
#if USE_PLL_TABLE
#include "quantized_oscillator.h"
// Frequency resolution of table can't be too small otherwise we end up with a large table
// This could cause inefficient memory access and cache misses
static auto PLL_TABLE = QuantizedOscillator(5, (int)(Fs));
#endif

// argument of complex number using floating points
// std::arg promotes the real and imaginary components to float which introduces a performance penalty
static inline float cargf(const std::complex<float>& x) {
    return std::atan2(x.imag(), x.real());
}

static inline viterbi_bit_t convert_to_viterbi_bit(const float x) {
    // DOC: ETSI EN 300 401
    // Referring to clause 14.5 - QPSK symbol mapper
    // phi = (1-2*b0) + (1-2*b1)*1j 
    // x0 = 1-2*b0, x1 = 1-2*b1
    // b = (1-x)/2

    // NOTE: Phil Karn's viterbi decoder is configured so that b => b' : (0,1) => (-127,+127)
    // Where b is the logical bit value, and b' is the value used for soft decision decoding
    // b' = (2*b-1) * 127 
    // b' = (1-x-1)*127
    // b' = -127*x
    constexpr float scale = (float)(SOFT_DECISION_VITERBI_HIGH);
    const float v = -x*scale;
    return (viterbi_bit_t)(v);
}

OFDM_Demod::OFDM_Demod(
    const OFDM_Params _params, 
    tcb::span<const std::complex<float>> _prs_fft_ref, 
    tcb::span<const int> _carrier_mapper)
:   params(_params), 
    null_power_dip_buffer(_params.nb_null_period),
    correlation_time_buffer(_params.nb_null_period + _params.nb_symbol_period)
{
    // NOTE: Using the FFT as an IFFT will cause the result to be reversed in time
    // Separate configurations for FFT and IFFT
    fft_cfg = kiss_fft_alloc((int)params.nb_fft, false, 0, 0);
    ifft_cfg = kiss_fft_alloc((int)params.nb_fft, true, 0, 0);

    // Initial state of demodulator
    state = State::FINDING_NULL_POWER_DIP;
    total_frames_desync = 0;
    total_frames_read = 0;
    freq_fine_offset = 0;
    is_null_start_found = false;
    is_null_end_found = false;
    signal_l1_average = 0;
    is_running = true;

    // Copy over the frequency domain values of our PRS (phase reference symbol) 
    // Correlation in time domain is the conjugate product in frequency domain
    {
        const size_t N = params.nb_fft;
        correlation_prs_fft_reference.resize(N);
        for (int i = 0; i < N; i++) {
            correlation_prs_fft_reference[i] = std::conj(_prs_fft_ref[i]);
        }
    }

    // DOC: ETSI EN 300 401
    // Referring to clause 14.5 - QPSK symbol mapper
    // Our subcarriers for each symbol are distributed according to a one to one rule
    {
        const size_t N = (size_t)params.nb_data_carriers;
        carrier_mapper.resize(N);
        std::copy_n(_carrier_mapper.begin(), N, carrier_mapper.begin());
    }

    // Double buffer ingest so our reader thread isn't blocked and drops samples from rtl_sdr.exe
    {
        const size_t N = params.nb_frame_symbols*params.nb_symbol_period + params.nb_null_period;
        active_buffer.Resize(N);
        inactive_buffer.Resize(N);
    }

    // Data structures to read all 76 symbols + NULL symbol and perform demodulation 
    pipeline_fft_buffer.resize(params.nb_fft*(params.nb_frame_symbols+1));
    {
        const size_t nb_dqpsk_symbols = params.nb_frame_symbols-1;
        const size_t N = params.nb_fft*nb_dqpsk_symbols;
        pipeline_dqpsk_vec_buffer.resize(N);
        pipeline_dqpsk_buffer.resize(N);
    }
    {
        const size_t nb_dqpsk_symbols = params.nb_frame_symbols-1;
        const size_t nb_bits = params.nb_data_carriers*nb_dqpsk_symbols*2;
        pipeline_out_bits.resize(nb_bits);
    }
    pipeline_fft_mag_buffer.resize(params.nb_fft);

    // Data structures used for fine frequency synchronisation using the PRS correlation
    correlation_impulse_response.resize(params.nb_fft);
    correlation_fft_buffer.resize(params.nb_fft);

    // Setup our multithreaded processing pipeline
    coordinator_thread = std::make_unique<OFDM_Demod_Coordinator_Thread>();
    {
        const size_t nb_all_syms = params.nb_frame_symbols+1;
        const size_t nb_threads = std::min(
            nb_all_syms, 
            (size_t)(std::thread::hardware_concurrency()));
            
        const size_t nb_sym_per_thread = nb_all_syms/nb_threads;
        for (int i = 0; i < nb_threads; i++) {
            const size_t symbol_start = i*nb_sym_per_thread;
            const bool is_last_thread = (i == (nb_threads-1));
            const size_t symbol_end = is_last_thread ? nb_all_syms : (i+1)*nb_sym_per_thread;
            pipelines.emplace_back(std::move(std::make_unique<OFDM_Demod_Pipeline_Thread>(
                symbol_start, symbol_end
            )));
        }
    }
    threads.emplace_back(std::move(std::make_unique<std::thread>(
        [this]() {
            while (is_running) {
                CoordinatorThread();
            }
            coordinator_thread->SignalEnd();
        }
    )));

    for (int i = 0; i < pipelines.size(); i++) {
        auto* pipeline = pipelines[i].get();
        auto* dependent_pipeline = ((i+1) >= pipelines.size()) ? NULL : pipelines[i+1].get();
        threads.emplace_back(std::move(std::make_unique<std::thread>(
            [this, pipeline, dependent_pipeline]() {
                while (is_running) {
                    PipelineThread(*pipeline, dependent_pipeline);
                }
            }
        )));
    }
}

OFDM_Demod::~OFDM_Demod() {
    // join all threads 
    coordinator_thread->Wait();
    is_running = false;
    coordinator_thread->Stop();
    for (auto& pipeline: pipelines) {
        pipeline->Stop();
    }
    for (auto& thread: threads) {
        thread->join();
    }

    // fft/ifft buffers
    kiss_fft_free(fft_cfg);
    kiss_fft_free(ifft_cfg);
}

void OFDM_Demod::Process(tcb::span<const std::complex<float>> buf) {
    UpdateSignalAverage(buf);

    const size_t N = buf.size();
    size_t curr_index = 0;
    while (curr_index < N) {
        auto* block = &buf[curr_index];
        const size_t N_remain = N-curr_index;

        switch (state) {

        // DOC: docs/DAB_implementation_in_SDR_detailed.pdf
        // Clause 3.12.1: Symbol timing synchronisation
        case State::FINDING_NULL_POWER_DIP:
            curr_index += FindNullPowerDip({block, N_remain});
            break;

        // DOC: docs/DAB_implementation_in_SDR_detailed.pdf
        // Clause 3.12.2: Frame synchronisation
        case State::CALCULATE_PRS_CORRELATION:
            curr_index += FindPRSCorrelation({block, N_remain});
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
        // DOC: ETSI EN 300 401
        // Referring to clause 14.5 - QPSK symbol mapper 
        case State::READING_BUFFER:
            curr_index += FillBuffer({block, N_remain});
            break;
        }
    }
}

void OFDM_Demod::Reset() {
    state = State::FINDING_NULL_POWER_DIP;
    correlation_time_buffer.SetLength(0);
    total_frames_desync++;

    // NOTE: We also reset fine frequency synchronisation since an incorrect value
    // can reduce performance of fine time synchronisation using the impulse response
    freq_fine_offset = 0.0f;
    signal_l1_average = 0.0f;
}

size_t OFDM_Demod::FindNullPowerDip(tcb::span<const std::complex<float>> buf) {
    // Clause 3.12.2 - Frame synchronisation using power detection
    // we run this if we dont have an initial estimate for the prs index
    // This can occur if:
    //      1. We just started the demodulator and need a quick estimate of OFDM start
    //      2. The PRS impulse response didn't have a sufficiently large peak

    // We analyse the average power of the signal using blocks of size K
    const size_t N = buf.size();
    const size_t K = cfg.signal_l1.nb_samples;
    const size_t M = N-K;

    const float null_start_thresh = signal_l1_average * cfg.null_l1_search.thresh_null_start;
    const float null_end_thresh = signal_l1_average * cfg.null_l1_search.thresh_null_end;

    // if the loop doesn't exit then we copy all samples into circular buffer
    size_t nb_read = N;
    for (size_t i = 0; i < M; i+=K) {
        auto* block = &buf[i];
        const float l1_avg = CalculateL1Average({block, K});
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

    null_power_dip_buffer.ConsumeBuffer({buf.data(), nb_read}, true);
    if (!is_null_end_found) {
        return nb_read;
    }

    // Copy null symbol into correlation buffer
    // This is done since our captured null symbol may actually contain parts of the PRS 
    // We do this so we can guarantee the full PRS is attained after fine time synchronisation using correlation
    const size_t L = null_power_dip_buffer.Length();
    const size_t start_index = null_power_dip_buffer.GetIndex();
    for (int i = 0; i < L; i++) {
        const size_t j = i+start_index;
        correlation_time_buffer[i] = null_power_dip_buffer[j];
    }
    
    is_null_start_found = false;
    is_null_end_found = false;
    correlation_time_buffer.SetLength(L);
    null_power_dip_buffer.SetLength(0);
    state = State::CALCULATE_PRS_CORRELATION;

    return nb_read;
}

size_t OFDM_Demod::FindPRSCorrelation(tcb::span<const std::complex<float>> buf) {
    // Clause 3.12.1 - Symbol timing synchronisation
    // To synchronise to start of the PRS we calculate the impulse response 
    const size_t nb_read = (size_t)correlation_time_buffer.ConsumeBuffer(buf);
    if (!correlation_time_buffer.IsFull()) {
        return nb_read;
    }

    auto corr_time_buf = correlation_time_buffer.GetData();
    auto corr_prs_buf = tcb::span(&corr_time_buf[params.nb_null_period], params.nb_fft);
    for (int i = 0; i < params.nb_fft; i++) {
        correlation_fft_buffer[i] = corr_prs_buf[i];
    }

    // Correlation in time domain is done by doing multiplication in frequency domain
    ApplyPLL(correlation_fft_buffer, correlation_fft_buffer, 0);
    kiss_fft(fft_cfg, (kiss_fft_cpx*)correlation_fft_buffer.data(), (kiss_fft_cpx*)correlation_fft_buffer.data());
    for (int i = 0; i < params.nb_fft; i++) {
        correlation_fft_buffer[i] *= correlation_prs_fft_reference[i];
    }

    // Get IFFT to get our correlation result
    kiss_fft(ifft_cfg, (kiss_fft_cpx*)correlation_fft_buffer.data(), (kiss_fft_cpx*)correlation_fft_buffer.data());
    for (int i = 0; i < params.nb_fft; i++) {
        const auto& v = correlation_fft_buffer[i];
        const float A = 20.0f*std::log10(std::abs(v));
        correlation_impulse_response[i] = A;
    }

    // calculate if we have a valid impulse response
    // if the peak is at least X dB above the mean, then we use that as our PRS starting index
    float impulse_avg = 0.0f;
    float impulse_max_value = correlation_impulse_response[0];
    int impulse_max_index = 0;
    for (int i = 0; i < params.nb_fft; i++) {
        const float v = correlation_impulse_response[i];
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
        state = State::FINDING_NULL_POWER_DIP;
        correlation_time_buffer.SetLength(0);
        total_frames_desync++;

        // NOTE: We also reset fine frequency synchronisation since an incorrect value
        // can reduce performance of fine time synchronisation using the impulse response
        freq_fine_offset = 0.0f;
        signal_l1_average = 0.0f;
        return nb_read;
    }

    // The PRS correlation lobe occurs just after the cyclic prefix
    // We actually want the index at the start of the cyclic prefix, so we adjust offset for that
    const int offset = impulse_max_index - (int)params.nb_cyclic_prefix;
    const int prs_start_index = (int)params.nb_null_period + offset;
    const int prs_length = (int)params.nb_symbol_period - offset;

    inactive_buffer.SetLength((size_t)prs_length);
    for (int i = 0; i < prs_length; i++) {
        const int j = prs_start_index+i;
        inactive_buffer[i] = correlation_time_buffer[j];
    }

    correlation_time_buffer.SetLength(0);
    state = State::READING_BUFFER;
    return nb_read;
}

size_t OFDM_Demod::FillBuffer(tcb::span<const std::complex<float>> buf) {
    const size_t nb_read = (size_t)inactive_buffer.ConsumeBuffer(buf);
    if (!inactive_buffer.IsFull()) {
        return nb_read;
    }

    // Copy the null symbol so we can use it in the PRS correlation step
    auto block = inactive_buffer.GetData();
    const size_t M = (size_t)inactive_buffer.Capacity();
    auto null_sym = tcb::span(&block[M-params.nb_null_period], params.nb_symbol_period);
    correlation_time_buffer.SetLength(params.nb_null_period);
    for (int i = 0; i < params.nb_null_period; i++) {
        correlation_time_buffer[i] = null_sym[i];
    }

    coordinator_thread->Wait();
    // double buffer
    std::swap(inactive_buffer, active_buffer);
    inactive_buffer.SetLength(0);
    // launch all our worker threads
    coordinator_thread->Start();

    state = State::CALCULATE_PRS_CORRELATION;
    return nb_read;
}

void OFDM_Demod::CoordinatorThread() {
    coordinator_thread->WaitStart();
    if (coordinator_thread->IsStopped()) {
        return;
    }
    for (auto& pipeline: pipelines) {
        pipeline->Start();
    }

    for (auto& pipeline: pipelines) {
        pipeline->WaitFFT();
    }

    // Clause 3.13.1 - Fraction frequency offset estimation
    // while we are synchronised update our fine frequency offset
    float average_cyclic_error = 0;
    for (auto& pipeline: pipelines) {
        const float cyclic_error = pipeline->GetAveragePhaseError();
        average_cyclic_error += cyclic_error;
    }
    average_cyclic_error /= (float)(params.nb_frame_symbols);
    UpdateFineFrequencyOffset(average_cyclic_error);

    for (auto& pipeline: pipelines) {
        pipeline->WaitEnd();
    }

    total_frames_read++;
    obs_on_ofdm_frame.Notify(pipeline_out_bits);
    coordinator_thread->SignalEnd();
}

void OFDM_Demod::PipelineThread(OFDM_Demod_Pipeline_Thread& thread_data, OFDM_Demod_Pipeline_Thread* dependent_thread_data) {
    const int symbol_start = (int)thread_data.GetSymbolStart();
    const int symbol_end = (int)thread_data.GetSymbolEnd();
    const int total_symbols = symbol_end-symbol_start;
    const int symbol_end_no_null = std::min(symbol_end, (int)params.nb_frame_symbols);
    const int symbol_end_dqpsk = std::min(symbol_end, (int)params.nb_frame_symbols-1);

    thread_data.WaitStart();
    if (thread_data.IsStopped()) {
        return;
    }

    // Cyclic prefix correlation
    auto pipeline_time_buffer = active_buffer.GetData();
    auto symbols_time_buf = tcb::span(
        &pipeline_time_buffer[symbol_start*params.nb_symbol_period],
        params.nb_symbol_period*total_symbols);

    // Correct for frequency offset in our signal
    {
        const float dt_start = CalculateTimeOffset(symbol_start*params.nb_symbol_period);
        ApplyPLL(symbols_time_buf, symbols_time_buf, dt_start); 
    }

    // Clause 3.13.1 - Fraction frequency offset estimation
    // get phase error using cyclic prefix (ignore null symbol)
    float total_phase_error = 0.0f;
    for (int i = symbol_start; i < symbol_end_no_null; i++) {
        auto sym_buf = tcb::span(
            &pipeline_time_buffer[i*params.nb_symbol_period],
            params.nb_symbol_period);
        const float cyclic_error = CalculateCyclicPhaseError(sym_buf);
        total_phase_error += cyclic_error;
    }
    thread_data.GetAveragePhaseError() = total_phase_error;

    // Clause 3.14.2 - FFT
    // calculate fft (include null symbol)
    for (int i = symbol_start; i < symbol_end; i++) {
        auto* sym_buf = &pipeline_time_buffer[i*params.nb_symbol_period];
        // Clause 3.14.1 - Cyclic prefix removal
        auto* data_buf = &sym_buf[params.nb_cyclic_prefix];
        auto* fft_buf = &pipeline_fft_buffer[i*params.nb_fft];
        kiss_fft(fft_cfg, 
            (kiss_fft_cpx*)data_buf,
            (kiss_fft_cpx*)fft_buf);
    }

    // Synchronise rest of worker threads here to calculate the DQPSK
    thread_data.SignalFFT();
    thread_data.StartDQPSK();

    const auto calculate_dqpsk = [this](int start, int end) {
        // Clause 3.15 - Differential demodulator
        // perform our differential qpsk decoding
        for (int i = start; i < end; i++) {
            auto* fft_buf_0     = &pipeline_fft_buffer[ i   *params.nb_fft];
            auto* fft_buf_1     = &pipeline_fft_buffer[(i+1)*params.nb_fft];
            auto* dqpsk_vec_buf = &pipeline_dqpsk_vec_buffer[i*params.nb_data_carriers];
            auto* dqpsk_buf     = &pipeline_dqpsk_buffer[i*params.nb_data_carriers];
            CalculateDQPSK(
                {fft_buf_1,     params.nb_fft}, 
                {fft_buf_0,     params.nb_fft}, 
                {dqpsk_vec_buf, params.nb_data_carriers}, 
                {dqpsk_buf,     params.nb_data_carriers});
        }

        // Map phase to viterbi bit
        for (int i = start; i < end; i++) {
            const size_t nb_viterbi_bits = params.nb_data_carriers*2;
            auto* dqpsk_buf = &pipeline_dqpsk_buffer[i*params.nb_data_carriers];
            auto* viterbi_bit_buf = &pipeline_out_bits[i*nb_viterbi_bits];
            CalculateViterbiBits(
                {dqpsk_buf,       params.nb_data_carriers}, 
                {viterbi_bit_buf, nb_viterbi_bits});
        }
    };

    calculate_dqpsk(symbol_start, symbol_end_dqpsk-1);
    // Get DQPSK result for last symbol in this thread 
    // which is dependent on other threads finishing
    if (dependent_thread_data != NULL) {
        dependent_thread_data->WaitDQPSK();
    }
    calculate_dqpsk(symbol_end_dqpsk-1, symbol_end_dqpsk);

    // TODO: optional - and we need to calculate the average
    // Calculate symbol magnitude (include null symbol)
    // for (int i = symbol_start; i < symbol_end; i++) {
    //     auto* fft_buf = &pipeline_fft_buffer[i*nb_fft_length];
    //     auto* fft_mag_buf = &pipeline_fft_mag_buffer[i*nb_fft_length];
    //     CalculateMagnitude(fft_buf, fft_mag_buf);
    // }

    thread_data.SignalEnd();
}

float OFDM_Demod::ApplyPLL(
    tcb::span<const std::complex<float>> x, 
    tcb::span<std::complex<float>> y, 
    const float dt0)
{
    #if !USE_PLL_TABLE

    float dt = dt0;
    for (int i = 0; i < N; i++) {
        const auto pll = std::complex<float>(
            std::cos(dt),
            std::sin(dt));
        y[i] = x[i] * pll;
        dt += 2.0f * (float)M_PI * freq_fine_offset * Ts;
    }
    return dt;

    #else

    const int N = (int)x.size();
    const int K = (int)PLL_TABLE.GetFrequencyResolution();
    const int M = (int)PLL_TABLE.GetTableSize();
    const int step = (int)freq_fine_offset / (int)K;

    int dt = (int)(dt0);
    dt = ((dt % M) + M) % M;
    for (int i = 0; i < N; i++) {
        y[i] = x[i] * PLL_TABLE[dt];
        dt = (dt + step + M) % M;
    }
    return (float)(dt);

    #endif
}

float OFDM_Demod::CalculateTimeOffset(const size_t i) {
    #if !USE_PLL_TABLE
    return 2.0f * (float)M_PI * freq_fine_offset * Ts * (float)i;
    #else
    const int K = (int)PLL_TABLE.GetFrequencyResolution();
    const int M = (int)PLL_TABLE.GetTableSize();
    const int step = (int)freq_fine_offset / (int)K;
    const int j = (int)i;
    return (float)((step*j) % M);
    #endif
}

float OFDM_Demod::CalculateCyclicPhaseError(tcb::span<const std::complex<float>> sym) {
    // DOC: docs/DAB_implementation_in_SDR_detailed.pdf
    // Clause 3.13.1 - Fraction frequency offset estimation
    const size_t N = params.nb_cyclic_prefix;
    const size_t M = params.nb_fft;
    auto error_vec = std::complex<float>(0,0);
    for (int i = 0; i < N; i++) {
        error_vec += std::conj(sym[i]) * sym[M+i];
    }
    return cargf(error_vec);
}

void OFDM_Demod::CalculateDQPSK(
    tcb::span<const std::complex<float>> in0, 
    tcb::span<const std::complex<float>> in1, 
    tcb::span<std::complex<float>> out_vec, 
    tcb::span<float> out_phase)
{
    const int M = (int)params.nb_data_carriers/2;
    const int N_fft = (int)params.nb_fft;

    // DOC: docs/DAB_implementation_in_SDR_detailed.pdf
    // Clause 3.14.3 - Zero padding removal
    // We store the subcarriers that carry information
    for (int i = -M, subcarrier_index = 0; i <= M; i++) {
        // The DC bin carries no information
        if (i == 0) {
            continue;
        }
        const int fft_index = (N_fft+i) % N_fft;

        // arg(z1*~z0) = arg(z1)+arg(~z0) = arg(z1)-arg(z0)
        const auto phase_delta_vec = in1[fft_index] * std::conj(in0[fft_index]);
        const float phase = cargf(phase_delta_vec);
        out_vec[subcarrier_index] = phase_delta_vec;
        out_phase[subcarrier_index] = phase;
        subcarrier_index++;
    }
}

void OFDM_Demod::CalculateViterbiBits(
    tcb::span<const float> phase_buf, 
    tcb::span<viterbi_bit_t> bit_buf) {
    const size_t N = params.nb_data_carriers;

    // DOC: ETSI EN 300 401
    // Referring to clause 14.5 - QPSK symbol mapper 
    // Deinterleave the subcarriers using carrier mapper
    // For an OFDM symbol with 2K bits, the nth symbol uses bits i and i+K
    for (int i = 0; i < N; i++) {
        const size_t j = carrier_mapper[i];
        const float phase = phase_buf[j];
        bit_buf[i]   = convert_to_viterbi_bit(std::cos(phase));
        bit_buf[N+i] = convert_to_viterbi_bit(-std::sin(phase));
    }
}

void OFDM_Demod::UpdateFineFrequencyOffset(const float cyclic_error) {
    // DOC: docs/DAB_implementation_in_SDR_detailed.pdf
    // Clause 3.13.1 - Fraction frequency offset estimation
    // determine the phase error using cyclic prefix
    const float ofdm_freq_spacing = (float)(params.freq_carrier_spacing);
    const float fine_freq_adjust = cyclic_error/(float)M_PI * ofdm_freq_spacing/2.0f;
    freq_fine_offset -= cfg.fine_freq_update_beta*fine_freq_adjust;
    freq_fine_offset = std::fmod(freq_fine_offset, ofdm_freq_spacing/2.0f);
}

void OFDM_Demod::CalculateMagnitude(
    tcb::span<const std::complex<float>> fft_buf, 
    tcb::span<float> mag_buf) 
{
    const size_t N = params.nb_fft;
    const size_t M = N/2;
    for (int i = 0; i < N; i++) {
        const size_t j = (i+M) % N;
        const float x = 20.0f*std::log10(std::abs(fft_buf[j]));
        mag_buf[i] = x;
    }
}

float OFDM_Demod::CalculateL1Average(tcb::span<const std::complex<float>> block) {
    const size_t N = block.size();
    float l1_avg = 0.0f;
    for (int i = 0; i < N; i++) {
        auto& v = block[i];
        l1_avg += std::abs(v.real()) + std::abs(v.imag());
    }
    l1_avg /= (float)N;
    return l1_avg;
}

void OFDM_Demod::UpdateSignalAverage(tcb::span<const std::complex<float>> block) {
    const size_t N = block.size();
    const size_t K = (size_t)cfg.signal_l1.nb_samples;
    const size_t M = N-K;
    const size_t L = K*cfg.signal_l1.nb_decimate;
    const float beta = cfg.signal_l1.update_beta;

    for (size_t i = 0; i < M; i+=L) {
        auto* buf = &block[i];
        const float l1_avg = CalculateL1Average({buf, K});
        signal_l1_average = 
            (beta)*signal_l1_average +
            (1.0f-beta)*l1_avg;
    }
}
