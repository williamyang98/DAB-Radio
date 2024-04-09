#pragma once

#include <stddef.h>
#include <stdint.h>
#include <complex>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include "utility/aligned_allocator.hpp"
#include "utility/observable.h"
#include "utility/span.h"
#include "viterbi_config.h"
#include "./circular_buffer.h"
#include "./ofdm_frame_buffer.h"
#include "./ofdm_params.h"
#include "./reconstruction_buffer.h"

struct fftwf_plan_s;

class OFDM_Demod_Pipeline;
class OFDM_Demod_Coordinator;

struct OFDM_Demod_Config {
    struct {
        float update_beta = 0.95f;
        int nb_samples = 100;
        int nb_decimate = 5;
    } signal_l1;
    struct {
        float thresh_null_start = 0.35f;
        float thresh_null_end = 0.75f;
    } null_l1_search;
    struct {
        // fine freq sync
        float fine_freq_update_beta = 0.9f;
        // coarse freq sync
        bool is_coarse_freq_correction = true;
        float max_coarse_freq_correction_norm = 0.5f; // normalised to sampling frequency
        float coarse_freq_slow_beta = 0.1f;
        // fine time sync
        float impulse_peak_threshold_db = 20.0f;
        float impulse_peak_distance_probability = 0.15f;
    } sync;
};

class OFDM_Demod 
{
public:
    enum State {
        FINDING_NULL_POWER_DIP,
        READING_NULL_AND_PRS,
        RUNNING_COARSE_FREQ_SYNC,
        RUNNING_FINE_TIME_SYNC,
        READING_SYMBOLS,
    };
private:
    OFDM_Demod_Config m_cfg;
    State m_state;
    const OFDM_Params m_params;
    // statistics
    int m_total_frames_read;
    int m_total_frames_desync;
    // time and frequency correction
    std::mutex m_mutex_freq_fine_offset;
    bool m_is_found_coarse_freq_offset;
    float m_freq_coarse_offset;
    float m_freq_fine_offset;
    int m_fine_time_offset;
    // null power dip search
    bool m_is_null_start_found;
    bool m_is_null_end_found;
    float m_signal_l1_average;
    // fft
    fftwf_plan_s* m_fft_plan;
    fftwf_plan_s* m_ifft_plan;
    // threads
    std::unique_ptr<OFDM_Demod_Coordinator> m_coordinator;
    std::vector<std::unique_ptr<OFDM_Demod_Pipeline>> m_pipelines;
    std::unique_ptr<std::thread> m_coordinator_thread;
    std::vector<std::unique_ptr<std::thread>> m_pipeline_threads;
    // callback for when ofdm is completed
    Observable<tcb::span<const viterbi_bit_t>> m_obs_on_ofdm_frame;
    // Joint memory allocation block
    std::vector<uint8_t, AlignedAllocator<uint8_t>> m_joint_data_block;
    // 1. pipeline reader double buffer
    OFDM_Frame_Buffer<std::complex<float>> m_active_buffer;
    OFDM_Frame_Buffer<std::complex<float>> m_inactive_buffer;
    tcb::span<uint8_t> m_active_buffer_data;
    tcb::span<uint8_t> m_inactive_buffer_data;
    // 2. fine time and coarse frequency synchronisation using time/frequency correlation
    CircularBuffer<std::complex<float>> m_null_power_dip_buffer;
    ReconstructionBuffer<std::complex<float>> m_correlation_time_buffer;
    tcb::span<std::complex<float>>    m_null_power_dip_buffer_data;
    tcb::span<std::complex<float>>    m_correlation_time_buffer_data;
    tcb::span<float>                  m_correlation_impulse_response;
    tcb::span<float>                  m_correlation_frequency_response;
    tcb::span<std::complex<float>>    m_correlation_fft_buffer;
    tcb::span<std::complex<float>>    m_correlation_ifft_buffer;
    tcb::span<std::complex<float>>    m_correlation_prs_fft_reference;
    tcb::span<std::complex<float>>    m_correlation_prs_time_reference;
    // 3. pipeline demodulation
    tcb::span<std::complex<float>>    m_pipeline_fft_buffer;
    tcb::span<std::complex<float>>    m_pipeline_dqpsk_vec_buffer;
    tcb::span<viterbi_bit_t>          m_pipeline_out_bits;
    // 4. carrier frequency deinterleaving
    tcb::span<int> m_carrier_mapper;
public:
    OFDM_Demod(
        const OFDM_Params& params, 
        const tcb::span<const std::complex<float>> prs_fft_ref, 
        const tcb::span<const int> carrier_mapper,
        int nb_desired_threads=0);
    ~OFDM_Demod();
    // threads use lambdas which take in the this pointer
    // therefore we disable move/copy semantics to preservce its memory location
    OFDM_Demod(OFDM_Demod&) = delete;
    OFDM_Demod(OFDM_Demod&&) = delete;
    OFDM_Demod& operator=(OFDM_Demod&) = delete;
    OFDM_Demod& operator=(OFDM_Demod&&) = delete;
    void Process(tcb::span<const std::complex<float>> block);
    void Reset();
public:
    OFDM_Params GetOFDMParams() const { return m_params; }
    State GetState() const { return m_state; }
    auto& GetConfig() { return m_cfg; }
    const auto& GetConfig() const { return m_cfg; }
    float GetSignalAverage() const { return m_signal_l1_average; }
    float GetFineFrequencyOffset() const { return m_freq_fine_offset; }
    float GetCoarseFrequencyOffset() const { return m_freq_coarse_offset; }
    float GetNetFrequencyOffset() const { return m_freq_fine_offset + m_freq_coarse_offset; }
    int GetFineTimeOffset() const { return m_fine_time_offset; }
    int GetTotalFramesRead() const { return m_total_frames_read; }
    int GetTotalFramesDesync() const { return m_total_frames_desync; }
    tcb::span<const std::complex<float>> GetFrameFFT() const { return m_pipeline_fft_buffer; }
    tcb::span<const std::complex<float>> GetFrameDataVec() const { return m_pipeline_dqpsk_vec_buffer; }
    tcb::span<const viterbi_bit_t> GetFrameDataBits() const { return m_pipeline_out_bits; }
    tcb::span<const float> GetImpulseResponse() const { return m_correlation_impulse_response; }
    tcb::span<const float> GetCoarseFrequencyResponse() const { return m_correlation_frequency_response; }
    tcb::span<const std::complex<float>> GetCorrelationTimeBuffer() const { return m_correlation_time_buffer; }
    auto& On_OFDM_Frame() { return m_obs_on_ofdm_frame; }
private:
    size_t FindNullPowerDip(tcb::span<const std::complex<float>> buf);
    size_t ReadNullPRS(tcb::span<const std::complex<float>> buf);
    size_t RunCoarseFreqSync(tcb::span<const std::complex<float>> buf);
    size_t RunFineTimeSync(tcb::span<const std::complex<float>> buf);
    size_t ReadSymbols(tcb::span<const std::complex<float>> buf);
private:
    void CreateThreads(int nb_desired_threads);
    bool CoordinatorThread();
    bool PipelineThread(OFDM_Demod_Pipeline& thread_data, OFDM_Demod_Pipeline* dependent_thread_data);
private:
    float CalculateTimeOffset(const size_t i, const float freq_offset);
    float CalculateCyclicPhaseError(tcb::span<const std::complex<float>> sym);
    float CalculateFineFrequencyError(const float cyclic_phase_error);
    void CalculateDQPSK(
        tcb::span<const std::complex<float>> in0, tcb::span<const std::complex<float>> in1, 
        tcb::span<std::complex<float>> out_vec);
    void CalculateViterbiBits(tcb::span<const std::complex<float>> vec_buf, tcb::span<viterbi_bit_t> bit_buf);
    void CalculateFFT(tcb::span<const std::complex<float>> fft_in, tcb::span<std::complex<float>> fft_out);
    void CalculateIFFT(tcb::span<const std::complex<float>> fft_in, tcb::span<std::complex<float>> fft_out);
    void CalculateRelativePhase(tcb::span<const std::complex<float>> fft_in, tcb::span<std::complex<float>> arg_out);
    void CalculateMagnitude(tcb::span<const std::complex<float>> fft_buf, tcb::span<float> mag_buf);
    float CalculateL1Average(tcb::span<const std::complex<float>> block);
    void UpdateSignalAverage(tcb::span<const std::complex<float>> block);
    void UpdateFineFrequencyOffset(const float delta);
};

