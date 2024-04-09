#define _USE_MATH_DEFINES
#include <cmath>
#include "./ofdm_demodulator.h"
#include <stddef.h>
#include <algorithm>
#include <complex>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <fftw3.h>
#include "detect_architecture.h"
#include "simd_flags.h" // NOLINT
#include "utility/joint_allocate.h"
#include "utility/span.h"
#include "viterbi_config.h"
#include "./dsp/apply_pll.h"
#include "./dsp/complex_conj_mul_sum.h"
#include "./ofdm_demodulator_threads.h"
#include "./ofdm_params.h"

#define PROFILE_ENABLE 1
#include "./profiler.h"

// NOTE: Determine correct alignment for FFTW3 buffers
#if defined(__ARCH_X86__)
    #if defined(__AVX__)
        #pragma message("OFDM_DEMOD FFTW3 buffers aligned to x86 AVX 256bits")
        constexpr size_t ALIGN_AMOUNT = 32;
    #elif defined(__SSE__)
        #pragma message("OFDM_DEMOD FFTW3 buffers aligned to x86 SSE 128bits")
        constexpr size_t ALIGN_AMOUNT = 16;
    #else
        #pragma message("OFDM_DEMOD FFTW3 buffers unaligned for x86 SCALAR")
        constexpr size_t ALIGN_AMOUNT = 16;
    #endif
#elif defined(__ARCH_AARCH64__)
    #pragma message("OFDM_DEMOD FFTW3 buffers aligned for ARM AARCH64 NEON 128bits")
    constexpr size_t ALIGN_AMOUNT = 16;
#else
    #pragma message("OFDM_DEMOD FFTW3 buffers unaligned for crossplatform SCALAR")
    constexpr size_t ALIGN_AMOUNT = 16;
#endif

constexpr float TWO_PI = float(M_PI) * 2.0f; // NOLINT


// DOC: docs/DAB_implementation_in_SDR_detailed.pdf
// NOTE: Unless specified otherwise all clauses referenced belong to the above documentation

// Receive the real/imaginary component of our data carrier
// Determine the bit value associated with it
// Return the bit value as a soft decision bit 
// - Hard decision bit: 0 or 1
// - Soft decision bit: Between -A and A
// We do this since our Viterbi decoder works with soft decision bits
static inline 
viterbi_bit_t convert_to_viterbi_bit(const float x) {
    // Clause 3.4.2 - QPSK symbol mapper
    // phi = (1-2*b0) + (1-2*b1)*1j
    // x0 = 1-2*b0, x1 = 1-2*b1
    // b = (1-x)/2

    // NOTE: Phil Karn's viterbi decoder is configured so that b => b' : (0,1) => (-A,+A)
    // Where b is the logical bit value, and b' is the value used for soft decision decoding
    // b' = (2*b-1) * A 
    // b' = (1-x-1)*A
    // b' = -A*x
    constexpr float scale = (float)(SOFT_DECISION_VITERBI_HIGH);
    const float v = -x*scale;
    return (viterbi_bit_t)(v);
}

template <typename ... T>
static void ApplyPLL(T... args) {
    PROFILE_BEGIN_FUNC();
    apply_pll_auto(std::forward<T>(args)...);
}

OFDM_Demod::OFDM_Demod(
    const OFDM_Params& params,
    const tcb::span<const std::complex<float>> prs_fft_ref, 
    const tcb::span<const int> carrier_mapper,
    int nb_desired_threads)
:   m_params(params), 
    m_active_buffer(params, m_active_buffer_data, ALIGN_AMOUNT),
    m_inactive_buffer(params, m_inactive_buffer_data, ALIGN_AMOUNT),
    m_null_power_dip_buffer(m_null_power_dip_buffer_data),
    m_correlation_time_buffer(m_correlation_time_buffer_data)
{
    // NOTE: Allocating joint block for better memory locality as well as alignment requirements
    //       Alignment is required for FFTW3 to use SIMD instructions which increases performance
    m_joint_data_block = AllocateJoint(
        m_carrier_mapper,                 BufferParameters{ m_params.nb_data_carriers }, 
        // Fine time correlation and coarse frequency correction
        m_null_power_dip_buffer_data,     BufferParameters{ m_params.nb_null_period },
        m_correlation_time_buffer_data,   BufferParameters{ m_params.nb_null_period + m_params.nb_symbol_period },
        m_correlation_prs_fft_reference,  BufferParameters{ m_params.nb_fft, ALIGN_AMOUNT },
        m_correlation_prs_time_reference, BufferParameters{ m_params.nb_fft, ALIGN_AMOUNT },
        m_correlation_impulse_response,   BufferParameters{ m_params.nb_fft, ALIGN_AMOUNT },
        m_correlation_frequency_response, BufferParameters{ m_params.nb_fft, ALIGN_AMOUNT },
        m_correlation_fft_buffer,         BufferParameters{ m_params.nb_fft, ALIGN_AMOUNT }, 
        m_correlation_ifft_buffer,        BufferParameters{ m_params.nb_fft, ALIGN_AMOUNT }, 
        // Double buffer ingest so our reader thread isn't blocked and drops samples from rtl_sdr.exe
        m_active_buffer_data,             BufferParameters{ m_active_buffer.GetTotalBufferBytes(), m_active_buffer.GetAlignment() },
        m_inactive_buffer_data,           BufferParameters{ m_inactive_buffer.GetTotalBufferBytes(), m_inactive_buffer.GetAlignment() },
        // Data structures to read all 76 symbols + NULL symbol and perform demodulation 
        m_pipeline_fft_buffer,            BufferParameters{ (m_params.nb_frame_symbols+1)*m_params.nb_fft, ALIGN_AMOUNT },
        m_pipeline_dqpsk_vec_buffer,      BufferParameters{ (m_params.nb_frame_symbols-1)*m_params.nb_fft, ALIGN_AMOUNT },
        m_pipeline_out_bits,              BufferParameters{ (m_params.nb_frame_symbols-1)*m_params.nb_data_carriers*2 }
    );

    m_fft_plan = fftwf_plan_dft_1d((int)m_params.nb_fft, nullptr, nullptr, FFTW_FORWARD, FFTW_ESTIMATE);
    m_ifft_plan = fftwf_plan_dft_1d((int)m_params.nb_fft, nullptr, nullptr, FFTW_BACKWARD, FFTW_ESTIMATE);

    // Initial state of demodulator
    m_state = State::FINDING_NULL_POWER_DIP;
    m_total_frames_desync = 0;
    m_total_frames_read = 0;
    m_is_found_coarse_freq_offset = false;
    m_freq_coarse_offset = 0;
    m_freq_fine_offset = 0;
    m_fine_time_offset = 0;
    m_is_null_start_found = false;
    m_is_null_end_found = false;
    m_signal_l1_average = 0;

    // Clause 3.12.1 - Fine time synchronisation
    // Correlation in time domain is the conjugate product in frequency domain
    for (size_t i = 0; i < m_params.nb_fft; i++) {
        m_correlation_prs_fft_reference[i] = std::conj(prs_fft_ref[i]);
    }

    // Clause 3.13.2 - Coarse frequency synchronisation
    // Correlation in frequency domain is the conjugate product in time domain
    CalculateRelativePhase(prs_fft_ref, m_correlation_prs_time_reference);
    CalculateIFFT(m_correlation_prs_time_reference, m_correlation_prs_time_reference);
    for (size_t i = 0; i < m_params.nb_fft; i++) {
        m_correlation_prs_time_reference[i] = std::conj(m_correlation_prs_time_reference[i]);
    }

    // Clause 3.16.1 - Frequency deinterleaving
    std::copy_n(carrier_mapper.begin(), m_params.nb_data_carriers, m_carrier_mapper.begin());

    CreateThreads(nb_desired_threads);
}

void OFDM_Demod::CreateThreads(int nb_desired_threads) {
    const int nb_syms = (int)m_params.nb_frame_symbols+1;
    const int total_system_threads = (int)std::thread::hardware_concurrency();

    int nb_threads = 0; 
    // Manually set number of threads
    if (nb_desired_threads > 0) {
        nb_threads = std::min(nb_syms, nb_desired_threads);
    // Automatically determine
    } else {
        nb_threads = std::min(nb_syms, total_system_threads);
        // NOTE: If we have a multicore system then
        //       Let one thread be used for fine time sync, coarse freq sync and data reading 
        //       The other threads are used for parallel processing of an OFDM frame
        if (nb_threads > 1) {
            nb_threads -= 1;
        } 
    }

    // Setup our multithreaded processing pipeline
    m_coordinator = std::make_unique<OFDM_Demod_Coordinator>();
    {
        int symbol_start = 0;    
        for (int i = 0; i < nb_threads; i++) {
            const bool is_last_thread = (i == (nb_threads-1));

            const int nb_syms_remain = (nb_syms-symbol_start);
            const int nb_threads_remain = (nb_threads-i);
            const int nb_syms_in_thread = (int)std::ceil((float)nb_syms_remain / (float)nb_threads_remain);
            const int symbol_end = is_last_thread ? nb_syms : (symbol_start+nb_syms_in_thread);
            m_pipelines.emplace_back(std::make_unique<OFDM_Demod_Pipeline>(
                symbol_start, symbol_end
            ));
            symbol_start = symbol_end;
        }
    }

    // Create coordinator thread
    m_coordinator_thread = std::make_unique<std::thread>(
        [this]() {
            PROFILE_TAG_THREAD("OFDM_Demod::CoordinatorThread");
            while (CoordinatorThread());
        }
    );

    // Create pipeline threads
    for (size_t i = 0; i < m_pipelines.size(); i++) {
        auto& pipeline = *(m_pipelines[i].get());

        // Some pipelines depend on data being processed in other pipelines
        const size_t dependent_pipeline_index = i+1;
        OFDM_Demod_Pipeline* dependent_pipeline = nullptr;
        if (dependent_pipeline_index < m_pipelines.size()) {
            dependent_pipeline = m_pipelines[dependent_pipeline_index].get();
        }

        m_pipeline_threads.emplace_back(std::make_unique<std::thread>(
            [this, &pipeline, dependent_pipeline]() {
                PROFILE_TAG_THREAD("OFDM_Demod::PipelineThread");
                PROFILE_TAG_DATA_THREAD(std::optional(InstrumentorThread::Descriptor{pipeline.GetSymbolStart(), pipeline.GetSymbolEnd()}));
                while (PipelineThread(pipeline, dependent_pipeline));
            }
        ));
    }
}

OFDM_Demod::~OFDM_Demod() {
    // Stop coordinator first so pipelines can finish properly
    m_coordinator->Stop();
    m_coordinator_thread->join();
    // Stop pipelines after coordinator has stopped
    for (auto& pipeline: m_pipelines) {
        pipeline->Stop();
    }
    for (auto& pipeline_thread: m_pipeline_threads) {
        pipeline_thread->join();
    }

    // fft/ifft buffers
    fftwf_destroy_plan(m_fft_plan);
    fftwf_destroy_plan(m_ifft_plan);
}

// Thread 1: Read frame data at start of frame
// Clause 3.12.1: Symbol timing synchronisation
// Clause 3.12.2: Frame synchronisation
// Clause 3.13.2 Integral frequency offset estimation
void OFDM_Demod::Process(tcb::span<const std::complex<float>> buf) {
    PROFILE_TAG_THREAD("OFDM_Demod::ProcessThread");
    PROFILE_ENABLE_TRACE_LOGGING(true);
    PROFILE_ENABLE_TRACE_LOGGING_CONTINUOUS(true);
    PROFILE_BEGIN_FUNC();

    UpdateSignalAverage(buf);

    const size_t N = buf.size();
    size_t curr_index = 0;
    while (curr_index < N) {
        auto* block = &buf[curr_index];
        const size_t N_remain = N-curr_index;

        switch (m_state) {

        // Clause 3.12.2: Frame synchronisation
        case State::FINDING_NULL_POWER_DIP:
            curr_index += FindNullPowerDip({block, N_remain});
            break;
        
        case State::READING_NULL_AND_PRS:
            curr_index += ReadNullPRS({block, N_remain});
            break;
        
        // Clause 3.13.2 Integral frequency offset estimation
        case State::RUNNING_COARSE_FREQ_SYNC:
            curr_index += RunCoarseFreqSync({block, N_remain});
            break;
        
        // Clause 3.12.1: Symbol timing synchronisation
        case State::RUNNING_FINE_TIME_SYNC:
            curr_index += RunFineTimeSync({block, N_remain});
            break;
        
        case State::READING_SYMBOLS:
            curr_index += ReadSymbols({block, N_remain});
            break;
        }
    }
}

void OFDM_Demod::Reset() {
    PROFILE_BEGIN_FUNC();
    m_state = State::FINDING_NULL_POWER_DIP;
    m_correlation_time_buffer.SetLength(0);
    m_total_frames_desync++;

    // NOTE: We also reset fine frequency synchronisation since an incorrect value
    // can reduce performance of fine time synchronisation using the impulse response
    m_is_found_coarse_freq_offset = false;
    m_freq_coarse_offset = 0;
    m_freq_fine_offset = 0;
    m_fine_time_offset = 0;
}

size_t OFDM_Demod::FindNullPowerDip(tcb::span<const std::complex<float>> buf) {
    PROFILE_BEGIN_FUNC();
    // Clause 3.12.2 - Frame synchronisation using power detection
    // we run this if we dont have an initial estimate for the prs index
    // This can occur if:
    //      1. We just started the demodulator and need a quick estimate of OFDM start
    //      2. The PRS impulse response didn't have a sufficiently large peak

    // We analyse the average power of the signal using blocks of size K
    const int N = (int)buf.size();
    const int K = (int)m_cfg.signal_l1.nb_samples;
    const int M = N-K;

    const float null_start_thresh = m_signal_l1_average * m_cfg.null_l1_search.thresh_null_start;
    const float null_end_thresh = m_signal_l1_average * m_cfg.null_l1_search.thresh_null_end;

    // if the loop doesn't exit then we copy all samples into circular buffer
    int nb_read = N;
    for (int i = 0; i < M; i+=K) {
        auto* block = &buf[i];
        const float l1_avg = CalculateL1Average({block, (size_t)K});
        if (m_is_null_start_found) {
            if (l1_avg > null_end_thresh) {
                m_is_null_end_found = true;
                nb_read = i+K;
                break;
            }
        } else {
            if (l1_avg < null_start_thresh) {
                m_is_null_start_found = true;
            }
        }
    }

    m_null_power_dip_buffer.ConsumeBuffer({buf.data(), (size_t)nb_read}, true);
    if (!m_is_null_end_found) {
        return (size_t)nb_read;
    }

    // Copy null symbol into correlation buffer
    // This is done since our captured null symbol may actually contain parts of the PRS 
    // We do this so we can guarantee the full start of the PRS is attained after fine time sync
    const size_t L = m_null_power_dip_buffer.Length();
    const size_t start_index = m_null_power_dip_buffer.GetIndex();
    for (size_t i = 0; i < L; i++) {
        const size_t j = i+start_index;
        m_correlation_time_buffer[i] = m_null_power_dip_buffer[j];
    }
 
    m_is_null_start_found = false;
    m_is_null_end_found = false;
    m_correlation_time_buffer.SetLength(L);
    m_null_power_dip_buffer.SetLength(0);
    m_state = State::READING_NULL_AND_PRS;

    return (size_t)nb_read;
}

size_t OFDM_Demod::ReadNullPRS(tcb::span<const std::complex<float>> buf) {
    PROFILE_BEGIN_FUNC();
    const size_t nb_read = m_correlation_time_buffer.ConsumeBuffer(buf);
    if (!m_correlation_time_buffer.IsFull()) {
        return nb_read;
    }

    m_state = State::RUNNING_COARSE_FREQ_SYNC;
    return nb_read;
}

size_t OFDM_Demod::RunCoarseFreqSync(tcb::span<const std::complex<float>> buf) {
    PROFILE_BEGIN_FUNC();
    // Clause: 3.13.2 Integral frequency offset estimation
    if (!m_cfg.sync.is_coarse_freq_correction) {
        m_freq_coarse_offset = 0;
        m_state = State::RUNNING_FINE_TIME_SYNC;
        return 0;
    }

    auto corr_time_buf = tcb::span(m_correlation_time_buffer);
    auto prs_sym = corr_time_buf.subspan(m_params.nb_null_period, m_params.nb_symbol_period);

    // To find the coarse frequency error correlate the FFT of the received and reference PRS
    // To mitigate effect of phase shifts we instead correlate the complex difference between consecutive FFT bins
    // arg(~z0*z1) = arg(z1)-arg(z0)

    // Step 1: Get FFT of received PRS 
    CalculateFFT(prs_sym, m_correlation_fft_buffer);

    // Step 2: Get complex difference between consecutive bins
    CalculateRelativePhase(m_correlation_fft_buffer, m_correlation_fft_buffer);

    // Step 3: Get IFFT so we can do correlation in frequency domain via product in time domain
    CalculateIFFT(m_correlation_fft_buffer, m_correlation_ifft_buffer);

    // Step 4: Conjugate product in time domain
    //         NOTE: correlation_prs_time_reference is already the conjugate
    for (size_t i = 0; i < m_params.nb_fft; i++) {
        m_correlation_ifft_buffer[i] *= m_correlation_prs_time_reference[i];
    }

    // Step 5: Get FFT to get correlation in frequency domain
    CalculateFFT(m_correlation_ifft_buffer, m_correlation_fft_buffer);

    // Step 6: Get magnitude spectrum so we can find the correlation peak
    CalculateMagnitude(m_correlation_fft_buffer, m_correlation_frequency_response);

    // Step 7: Find the peak in our maximum coarse frequency error window
    // NOTE: A zero frequency error corresponds to a peak at nb_fft/2
    int max_carrier_offset = int(m_cfg.sync.max_coarse_freq_correction_norm * float(m_params.nb_fft));
    const int M = int(m_params.nb_fft/2);
    if (max_carrier_offset < 0) max_carrier_offset = 0;
    if (max_carrier_offset > M) max_carrier_offset = M;
    int max_index = -max_carrier_offset;
    float max_value = m_correlation_frequency_response[max_index+M];
    for (int i = -max_carrier_offset; i <= max_carrier_offset; i++) {
        const int fft_index = i+M;
        if (fft_index == int(m_params.nb_fft)) continue;
        const float value = m_correlation_frequency_response[fft_index];
        if (value > max_value) {
            max_value = value;
            max_index = i;
        }
    }

    // Step 8: Determine the coarse frequency offset 
    // NOTE: We get the frequency offset in terms of FFT bins which we convert to normalised Hz
    //       Lerp peak between neighbouring fft bins based on magnitude for more accurate estimate
    struct Peak {
        int index;
        float magnitude;
    };
    Peak peaks[3];
    const auto get_peak = [M,max_carrier_offset,this](int index) -> Peak {
        if (index < -max_carrier_offset) index = -max_carrier_offset;
        if (index >  max_carrier_offset) index =  max_carrier_offset;
        int fft_index = (index+M);
        if (fft_index >= int(m_params.nb_fft)) fft_index = int(m_params.nb_fft-1);
        const float magnitude_dB = m_correlation_frequency_response[fft_index];
        const float magnitude = std::pow(10.0f, magnitude_dB/20.0f);
        return Peak { fft_index-M, magnitude };
    };
    peaks[0] = get_peak(max_index-1);
    peaks[1] = get_peak(max_index+0);
    peaks[2] = get_peak(max_index+1);
    float peak_sum = 0.0f;
    float lerp_peak = 0.0f;
    for (const auto& peak: peaks) { peak_sum += peak.magnitude; }
    for (const auto& peak: peaks) { lerp_peak += float(peak.index)*peak.magnitude/peak_sum; }
    const float predicted_freq_coarse_offset = -lerp_peak / float(m_params.nb_fft);
    const float error = predicted_freq_coarse_offset-m_freq_coarse_offset;

    // Step 9: Determine if this is an large or small correction
    // Case A: If we have a large correction, we need to immediately update or subsequent processing
    //         will be performed on a horribly out of sync signal
    // Case B: If we have a small correction, i.e. within one FFT bin, then slowly update
    //         This is because we may end up in a state where the offset lies between two adjacent FFT bins
    //         This can cause the coarse frequency correction to oscillate between those two adjacent bins
    //         We can reduce the update rate so the coarse frequency correction doesn't fluctuate too much
    const float large_offset_threshold = 1.5f / float(m_params.nb_fft);
    const bool is_large_correction = std::abs(error) > large_offset_threshold;

    // NOTE: We only do this gradual update if the coarse frequency offset was already found
    //       If we are getting the initial estimate we need the update to be instant
    //       otherwise the PRS fine time correlation step won't find a big enough impulse peak
    //       causing the entire process to reset
    const bool is_fast_update = is_large_correction || !m_is_found_coarse_freq_offset;
    const float beta = is_fast_update ? 1.0f : m_cfg.sync.coarse_freq_slow_beta;
    const float delta = beta*error;

    // Step 10: Update the coarse frequency offset
    m_freq_coarse_offset += delta;
    m_is_found_coarse_freq_offset = true;

    // Step 11: Counter adjust the fine frequency offset
    // In a near locked state the coarse frequency offset may fluctuate alot if it lies between two FFT bins
    // By counter adjusting the fine frequency offset, the combined coarse and fine frequency offset will be stable
    UpdateFineFrequencyOffset(-delta);

    m_state = State::RUNNING_FINE_TIME_SYNC;
    return 0;
}

size_t OFDM_Demod::RunFineTimeSync(tcb::span<const std::complex<float>> buf) {
    PROFILE_BEGIN_FUNC();
    // Clause 3.12.1 - Symbol timing synchronisation
    auto corr_time_buf = tcb::span(m_correlation_time_buffer);
    auto corr_prs_buf = corr_time_buf.subspan(m_params.nb_null_period, m_params.nb_symbol_period);

    // Correct for frequency offset before finding impulse response for best results
    const float freq_offset = m_freq_coarse_offset + m_freq_fine_offset;
    std::copy_n(corr_prs_buf.begin(), m_params.nb_fft, m_correlation_ifft_buffer.begin());
    ApplyPLL(m_correlation_ifft_buffer, m_correlation_ifft_buffer, freq_offset);

    // To synchronise to start of the PRS we calculate the impulse response 
    // Correlation in time domain is done by doing conjugate multiplication in frequency domain
    // NOTE: Our PRS FFT reference was conjugated in the constructor
    CalculateFFT(m_correlation_ifft_buffer, m_correlation_fft_buffer);
    for (size_t i = 0; i < m_params.nb_fft; i++) {
        m_correlation_fft_buffer[i] *= m_correlation_prs_fft_reference[i];
    }

    // Get IFFT to get our correlation result
    CalculateIFFT(m_correlation_fft_buffer, m_correlation_ifft_buffer);
    for (size_t i = 0; i < m_params.nb_fft; i++) {
        const auto& v = m_correlation_ifft_buffer[i];
        const float A = 20.0f*std::log10(std::abs(v));
        m_correlation_impulse_response[i] = A;
    }

    // Calculate if we have a valid impulse response
    // If the peak is at least X dB above the mean, then we use that as our PRS starting index
    float impulse_avg = 0.0f;
    float impulse_max_value = m_correlation_impulse_response[0];
    int impulse_max_index = 0;
    for (int i = 0; i < int(m_params.nb_fft); i++) {
        const float peak_value = m_correlation_impulse_response[i];

        // We expect that the correlation peak will at least be somewhere near where we expect it
        // When we are still locking on, the impulse response may have many peaks due to frequency offsets
        // This causes spurious desyncs when one of these other peaks are very far away
        // Thus we weigh the value of the peak with its distance from the expected location
        const int expected_peak_x = (int)m_params.nb_cyclic_prefix;
        const int distance_from_expectation = std::abs(expected_peak_x-i);
        const float norm_distance = (float)distance_from_expectation / (float)m_params.nb_symbol_period;
        const float decay_weight = 1.0f - m_cfg.sync.impulse_peak_distance_probability;
        const float probability = 1.0f - decay_weight * norm_distance;
        const float weighted_peak_value = probability*peak_value;

        impulse_avg += peak_value;
        if (weighted_peak_value > impulse_max_value) {
            impulse_max_value = weighted_peak_value;
            impulse_max_index = i;
        }
    }
    impulse_avg /= (float)m_params.nb_fft;

    // If the main lobe is insufficiently powerful we do not have a valid impulse response
    // This probably means we had a severe desync and should restart
    if ((impulse_max_value - impulse_avg) < m_cfg.sync.impulse_peak_threshold_db) {
        Reset();
        return 0;
    }

    // The PRS correlation lobe occurs just after the cyclic prefix
    // We actually want the index at the start of the cyclic prefix, so we adjust offset for that
    const int offset = impulse_max_index - (int)m_params.nb_cyclic_prefix;
    const int prs_start_index = (int)m_params.nb_null_period + offset;
    const int prs_length = (int)m_params.nb_symbol_period - offset;
    auto prs_buf = corr_time_buf.subspan(prs_start_index, prs_length);
    
    m_inactive_buffer.Reset();
    m_inactive_buffer.ConsumeBuffer(prs_buf);

    m_correlation_time_buffer.SetLength(0);
    m_fine_time_offset = offset;
    m_state = State::READING_SYMBOLS;
    return 0;
}

size_t OFDM_Demod::ReadSymbols(tcb::span<const std::complex<float>> buf) {
    PROFILE_BEGIN_FUNC();
    const size_t nb_read = m_inactive_buffer.ConsumeBuffer(buf);
    if (!m_inactive_buffer.IsFull()) {
        return nb_read;
    }

    // Copy the null symbol so we can use it in the PRS correlation step
    auto null_sym = m_inactive_buffer.GetNullSymbol();
    m_correlation_time_buffer.SetLength(m_params.nb_null_period);
    for (size_t i = 0; i < m_params.nb_null_period; i++) {
        m_correlation_time_buffer[i] = null_sym[i];
    }

    PROFILE_BEGIN(coordinator_wait);
    m_coordinator->WaitEnd();
    PROFILE_END(coordinator_wait);
    // double buffer
    std::swap(m_inactive_buffer_data, m_active_buffer_data);
    m_inactive_buffer.Reset();
    // launch all our worker threads
    PROFILE_BEGIN(coordinator_start);
    m_coordinator->SignalStart();
    PROFILE_END(coordinator_start);

    m_state = State::READING_NULL_AND_PRS;
    return nb_read;
}

// Thread 2: Coordinate pipeline threads and combine fine time synchronisation results
// Clause 3.13.1: Fractional frequency offset estimation
bool OFDM_Demod::CoordinatorThread() {
    PROFILE_BEGIN_FUNC();

    PROFILE_BEGIN(coordinator_wait_start);
    m_coordinator->WaitStart();
    PROFILE_END(coordinator_wait_start);

    if (m_coordinator->IsStopped()) {
        return false;
    }

    PROFILE_BEGIN(pipeline_workers);
    {
        PROFILE_BEGIN(pipeline_start);
        for (auto& pipeline: m_pipelines) {
            pipeline->SignalStart();
        }
        PROFILE_END(pipeline_start);

        PROFILE_BEGIN(pipeline_wait_phase_error);
        for (auto& pipeline: m_pipelines) {
            pipeline->WaitPhaseError();
        }
        PROFILE_END(pipeline_wait_phase_error);

        // Clause 3.13.1 - Fraction frequency offset estimation
        PROFILE_BEGIN(calculate_phase_error);
        float average_cyclic_error = 0;
        for (const auto& pipeline: m_pipelines) {
            const float cyclic_error = pipeline->GetAveragePhaseError();
            average_cyclic_error += cyclic_error;
        }
        average_cyclic_error /= float(m_params.nb_frame_symbols);
        // Calculate adjustments to fine frequency offset 
        const float fine_freq_error = CalculateFineFrequencyError(average_cyclic_error);
        const float beta = m_cfg.sync.fine_freq_update_beta;
        const float delta = -beta*fine_freq_error;
        UpdateFineFrequencyOffset(delta);
        PROFILE_END(calculate_phase_error);

        PROFILE_BEGIN(pipeline_wait_end);
        for (auto& pipeline: m_pipelines) {
            pipeline->WaitEnd();
        }
        PROFILE_END(pipeline_wait_end);

        PROFILE_BEGIN(coordinator_signal_end);
        m_coordinator->SignalEnd();
        PROFILE_END(coordinator_signal_end);
    }
    PROFILE_END(pipeline_workers);
    m_total_frames_read++;

    PROFILE_BEGIN(obs_on_ofdm_frame);
    m_obs_on_ofdm_frame.Notify(m_pipeline_out_bits);
    PROFILE_END(obs_on_ofdm_frame);

    return true;
}

// Thread 3xN: Process ofdm frame
// Clause 3.14: OFDM symbol demodulator
// Clause 3.14.1: Cyclic prefix removal
// Clause 3.14.2: FFT
// Clause 3.14.3: Zero padding removal from FFT (Only include the carriers that are associated with this OFDM transmitter)
// Clause 3.15: Differential demodulator
// Clause 3.16: Data demapper
// Clause 3.16.1: Frequency deinterleaving
// Clause 3.16.2: QPSK symbol demapper
bool OFDM_Demod::PipelineThread(OFDM_Demod_Pipeline& thread_data, OFDM_Demod_Pipeline* dependent_thread_data) {
    PROFILE_BEGIN_FUNC();

    const int symbol_start = (int)thread_data.GetSymbolStart();
    const int symbol_end = (int)thread_data.GetSymbolEnd();
    const int symbol_end_no_null = std::min(symbol_end, (int)m_params.nb_frame_symbols);
    const int symbol_end_dqpsk = std::min(symbol_end, (int)m_params.nb_frame_symbols-1);

    PROFILE_BEGIN(pipeline_wait_start);
    thread_data.WaitStart();
    PROFILE_END(pipeline_wait_start);

    if (thread_data.IsStopped()) {
        return false;
    }

    PROFILE_BEGIN(data_processing);

    // Fine and coarse frequency correction with PLL
    PROFILE_BEGIN(apply_pll);
    // NOTE: We create a local copy of the frequency offset since it
    //       can be changed in the reader thread due to coarse frequency correction
    const float frequency_offset = m_freq_coarse_offset + m_freq_fine_offset;
    for (int i = symbol_start; i < symbol_end; i++) {
        auto sym_buf = m_active_buffer.GetDataSymbol(i);
        const int sample_offset = i*(int)m_params.nb_symbol_period;
        const float dt_start = float(sample_offset) * frequency_offset;
        ApplyPLL(sym_buf, sym_buf, frequency_offset, dt_start); 
    }
    PROFILE_END(apply_pll);

    // Clause 3.13: Frequency offset estimation and correction
    // Clause 3.13.1 - Fraction frequency offset estimation
    // Get phase error using cyclic prefix (ignore null symbol)
    PROFILE_BEGIN(calculate_phase_error);
    float total_phase_error = 0.0f;
    for (int i = symbol_start; i < symbol_end_no_null; i++) {
        auto sym_buf = m_active_buffer.GetDataSymbol(i);
        const float cyclic_error = CalculateCyclicPhaseError(sym_buf);
        total_phase_error += cyclic_error;
    }
    thread_data.SetAveragePhaseError(total_phase_error);
    PROFILE_END(calculate_phase_error);

    // Signal to the coordinator thread our phase error
    PROFILE_BEGIN(pipeline_signal_phase_error);
    thread_data.SignalPhaseError();
    PROFILE_END(pipeline_signal_phase_error);

    // Clause 3.14.2 - FFT
    // Calculate fft (include null symbol)
    const auto calculate_fft = [this](int start, int end) {
        for (int i = start; i < end; i++) {
            auto sym_buf = m_active_buffer.GetDataSymbol(i);
            // Clause 3.14.1 - Cyclic prefix removal
            auto data_buf = sym_buf.subspan(m_params.nb_cyclic_prefix, m_params.nb_fft);
            auto fft_buf = m_pipeline_fft_buffer.subspan(i*m_params.nb_fft, m_params.nb_fft);
            CalculateFFT(data_buf, fft_buf);
        }
    };

    // Calculate FFT and notify threads which need this result for DQPSK
    // This way we don't hold up other threads waiting for these results
    PROFILE_BEGIN(calculate_dependent_fft);
    calculate_fft(symbol_start, symbol_start+1);
    PROFILE_END(calculate_dependent_fft);

    PROFILE_BEGIN(pipeline_signal_fft);
    thread_data.SignalFFT();
    PROFILE_END(pipeline_signal_fft);

    // These FFTs are only used by this thread for DQPSK 
    PROFILE_BEGIN(calculate_independent_fft);
    calculate_fft(symbol_start+1, symbol_end);
    PROFILE_END(calculate_independent_fft);

    // Clause 3.15 - Differential demodulator
    // perform our differential QPSK decoding
    const auto calculate_dqpsk = [this](int start, int end) {
        const size_t nb_viterbi_bits = m_params.nb_data_carriers*2;
        for (int i = start; i < end; i++) {
            PROFILE_BEGIN(calculate_dqpsk_symbol);
            auto fft_buf_0 = m_pipeline_fft_buffer.subspan((i+0)*m_params.nb_fft, m_params.nb_fft);
            auto fft_buf_1 = m_pipeline_fft_buffer.subspan((i+1)*m_params.nb_fft, m_params.nb_fft);
            auto dqpsk_vec_buf = m_pipeline_dqpsk_vec_buffer.subspan(i*m_params.nb_data_carriers, m_params.nb_data_carriers);
            auto viterbi_bit_buf = m_pipeline_out_bits.subspan(i*nb_viterbi_bits, nb_viterbi_bits);
            CalculateDQPSK(fft_buf_1, fft_buf_0, dqpsk_vec_buf);
            CalculateViterbiBits(dqpsk_vec_buf, viterbi_bit_buf);
        }
    };

    // Get DQPSK result for last symbol in this thread 
    // which is dependent on other threads finishing
    if (dependent_thread_data != nullptr) {
        PROFILE_BEGIN(calculate_independent_dqpsk);
        calculate_dqpsk(symbol_start, symbol_end_dqpsk-1);
        PROFILE_END(calculate_independent_dqpsk);

        PROFILE_BEGIN(dependent_pipeline_wait_fft);
        dependent_thread_data->WaitFFT();
        PROFILE_END(dependent_pipeline_wait_fft);

        PROFILE_BEGIN(calculate_dependent_dqpsk);
        calculate_dqpsk(symbol_end_dqpsk-1, symbol_end_dqpsk);
        PROFILE_END(calculate_dependent_dqpsk);
    } else {
        PROFILE_BEGIN(calculate_independent_dqpsk);
        calculate_dqpsk(symbol_start, symbol_end_dqpsk);
        PROFILE_END(calculate_independent_dqpsk);
    }

    PROFILE_BEGIN(pipeline_signal_end);
    thread_data.SignalEnd();
    PROFILE_END(pipeline_signal_end);

    return true;
}

float OFDM_Demod::CalculateCyclicPhaseError(tcb::span<const std::complex<float>> sym) {
    PROFILE_BEGIN_FUNC();
    // Clause 3.13.1 - Fraction frequency offset estimation
    const size_t N = m_params.nb_cyclic_prefix;
    const size_t M = m_params.nb_fft;
    auto x0 = sym.subspan(M, N);
    auto x1 = sym.subspan(0, N);
    auto error_vec = complex_conj_mul_sum_auto(x0, x1);
    return std::atan2(error_vec.imag(), error_vec.real());
}

float OFDM_Demod::CalculateFineFrequencyError(const float cyclic_phase_error) {
    PROFILE_BEGIN_FUNC();
    // Clause 3.13.1 - Fraction frequency offset estimation
    /*  Derivation of fine frequency error
        // Definition of cyclic prefix
        Prefix = e^jw0(t+T), Data = e^jw0t
        Since the prefix is equal to the data in an OFDM symbol
        w0(t+T) = w0t + 2*k*pi, k is an integer
        T = k*(2*pi)/w0                 (equ 1) 
     
        // Calculation of phase error (no frequency error)
        phi = conj(prefix) * data
        phi = e^-jw0(t+T) * e^jw0t
        phi = e^-jw0T = e^(-j*k*2*pi), where k is an integer
        error = arg(phi) = -2*pi*k = 0
    
        // Calculate of phase error (with frequency offset)
        Let w1 = fine frequency offset (w1 < w0)
        Prefix = e^jw0(t+T) * e^jw1(t+T) = e^j(w0+w1)(t+T)
        Data   = e^jw0t     * e^jw1t     = e^j(w0+w1)t
        phi = conj(prefix)*data
        phi = e^-j(w0+w1)(t+T) * e^j(w0+w1)t
        phi = e^-j(w0+w1)T
        error = arg(phi) 
        error = (w0+w1)T,
        error = (w0+w1)/w0 * 2*k*pi, substitute T using (equ 1)
        error = 2*k*pi + (w1/w0)*2*k*pi
        error = w1/w0 * 2*k*pi

        since |error| <= 2*pi and (w1/w0) < 1
        error = w1/w0 * 2*pi            (equ 2)
     
        // data (including prefix) is generated from ifft/fft on modulator side
        // wd = fft carrier frequency normalised to sampling frequency
        w0 = K*wd, where K is an integer
        error = w1/(K*wd) * 2*pi, substitute w0 using (equ 2)
        w1 = K * wd * error/(2*pi)

        since |w1| < wd, then K = 1
        w1 = wd * error/(2*pi)

    */
    const float fft_bin_spacing = 1.0f/float(m_params.nb_fft);
    const float fine_frequency_error = fft_bin_spacing * cyclic_phase_error/TWO_PI;
    return fine_frequency_error;
}

// Two threads may try to update the fine frequency offset simulataneously 
// Reader thread: Runs coarse frequency correction during frame sychronisation which also affects fine frequency offset
// Coordinator thread: Joins phase errors from pipeline thread and calculates average adjustment for fine frequency offset
void OFDM_Demod::UpdateFineFrequencyOffset(const float delta) {
    PROFILE_BEGIN_FUNC();
    const float fft_bin_spacing = 1.0f/float(m_params.nb_fft);
    // NOTE: If the fine frequency adjustment is just on the edge of overflowing
    //       We add enough margin to stop this from occuring
    const float fft_bin_margin = 1.01f;
    const float fft_bin_wrap = 0.5f * fft_bin_spacing * fft_bin_margin;

    auto lock = std::scoped_lock(m_mutex_freq_fine_offset);
    m_freq_fine_offset += delta;
    m_freq_fine_offset = std::fmod(m_freq_fine_offset, fft_bin_wrap);
}

void OFDM_Demod::CalculateDQPSK(
    tcb::span<const std::complex<float>> in0, 
    tcb::span<const std::complex<float>> in1, 
    tcb::span<std::complex<float>> out_vec)
{
    PROFILE_BEGIN_FUNC();
    const int M = (int)m_params.nb_data_carriers/2;
    const int N_fft = (int)m_params.nb_fft;

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
        out_vec[subcarrier_index] = phase_delta_vec;
        subcarrier_index++;
    }
}

void OFDM_Demod::CalculateViterbiBits(tcb::span<const std::complex<float>> vec_buf, tcb::span<viterbi_bit_t> bit_buf) {
    PROFILE_BEGIN_FUNC();
    const size_t N = m_params.nb_data_carriers;

    // Clause 3.16 - Data demapper
    for (size_t i = 0; i < N; i++) {
        // Clause 3.16.1 - Freuency deinterleaving
        const size_t j = m_carrier_mapper[i];
        const auto& vec = vec_buf[j];

        // const float A = std::abs(vec);
        // NOTE: Use the L1 norm since it doesn't truncate like L2 norm
        //       I.e. When real=imag, then we expect b0=A, b1=A
        //            But with L2 norm, we get b0=0.707*A, b1=0.707*A
        //                with L1 norm, we get b0=A, b1=A as expected
        const float A = std::max(std::abs(vec.real()), std::abs(vec.imag()));
        const auto norm_vec = vec / A;

        // Clause 3.16.2 - QPSK symbol demapper
        bit_buf[i]   = convert_to_viterbi_bit(+norm_vec.real());
        bit_buf[i+N] = convert_to_viterbi_bit(-norm_vec.imag());
    }
}

void OFDM_Demod::CalculateFFT(tcb::span<const std::complex<float>> fft_in, tcb::span<std::complex<float>> fft_out) {
    PROFILE_BEGIN_FUNC();
    fftwf_execute_dft(m_fft_plan, (fftwf_complex*)fft_in.data(), (fftwf_complex*)fft_out.data());
}

void OFDM_Demod::CalculateIFFT(tcb::span<const std::complex<float>> fft_in, tcb::span<std::complex<float>> fft_out) {
    PROFILE_BEGIN_FUNC();
    fftwf_execute_dft(m_ifft_plan, (fftwf_complex*)fft_in.data(), (fftwf_complex*)fft_out.data());
}

void OFDM_Demod::CalculateRelativePhase(tcb::span<const std::complex<float>> fft_in, tcb::span<std::complex<float>> arg_out) {
    PROFILE_BEGIN_FUNC();
    const int N = (int)m_params.nb_fft;
    for (int i = 0; i < (N-1); i++) {
        const auto vec = std::conj(fft_in[i]) * fft_in[i+1];
        arg_out[i] = vec;
    }
    arg_out[N-1] = {0,0};
}

void OFDM_Demod::CalculateMagnitude(tcb::span<const std::complex<float>> fft_buf, tcb::span<float> mag_buf) {
    PROFILE_BEGIN_FUNC();
    const size_t N = m_params.nb_fft;
    const size_t M = N/2;
    for (size_t i = 0; i < N; i++) {
        const size_t j = (i+M) % N;
        const float x = 20.0f*std::log10(std::abs(fft_buf[j]));
        mag_buf[i] = x;
    }
}

float OFDM_Demod::CalculateL1Average(tcb::span<const std::complex<float>> block) {
    PROFILE_BEGIN_FUNC();
    const size_t N = block.size();
    float l1_avg = 0.0f;
    for (size_t i = 0; i < N; i++) {
        auto& v = block[i];
        l1_avg += std::abs(v.real()) + std::abs(v.imag());
    }
    l1_avg /= (float)N;
    return l1_avg;
}

void OFDM_Demod::UpdateSignalAverage(tcb::span<const std::complex<float>> block) {
    PROFILE_BEGIN_FUNC();
    const size_t N = block.size();
    const size_t K = (size_t)m_cfg.signal_l1.nb_samples;
    if (N < K) return;
    const size_t M = N-K;
    const size_t L = K*m_cfg.signal_l1.nb_decimate;
    const float beta = m_cfg.signal_l1.update_beta;

    for (size_t i = 0; i < M; i+=L) {
        auto* buf = &block[i];
        const float l1_avg = CalculateL1Average({buf, K});
        m_signal_l1_average = 
            (beta)*m_signal_l1_average +
            (1.0f-beta)*l1_avg;
    }
}
