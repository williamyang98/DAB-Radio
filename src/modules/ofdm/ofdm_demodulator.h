#pragma once

#include <stdint.h>
#include <complex>
#include <thread>
#include <vector>
#include <memory>

#include "ofdm_params.h"
#include "utility/reconstruction_buffer.h"
#include "utility/circular_buffer.h"
#include "utility/observable.h"
#include "utility/span.h"
#include "viterbi_config.h"

typedef struct kiss_fft_state* kiss_fft_cfg;

class OFDM_Demod_Pipeline_Thread;
class OFDM_Demod_Coordinator_Thread;

class OFDM_Demod 
{
public:
    struct Config {
        struct {
            float update_beta = 0.95f;
            int nb_samples = 100;
            int nb_decimate = 5;
        } signal_l1;
        float fine_freq_update_beta = 0.5f;
        struct {
            float thresh_null_start = 0.35f;
            float thresh_null_end = 0.75f;
        } null_l1_search;
        float impulse_peak_threshold_db = 20.0f;
        float data_sym_magnitude_update_beta = 0.1f;
        struct {
            bool is_update_data_sym_mag = false;
            bool is_update_tii_sym_mag = false;
        } toggle_flags;
    };
    enum State {
        FINDING_NULL_POWER_DIP,
        CALCULATE_PRS_CORRELATION,
        READING_BUFFER
    };
private:
    bool is_running;
    Config cfg;
    State state;
    const OFDM_Params params;
    std::vector<int> carrier_mapper;
    // statistics
    int total_frames_read;
    int total_frames_desync;
    // ofdm frequency correction
    float freq_fine_offset;
    // null power dip search
    bool is_null_start_found;
    bool is_null_end_found;
    float signal_l1_average;
    // pipeline and ingest buffer
    ReconstructionBuffer<std::complex<float>> active_buffer;
    ReconstructionBuffer<std::complex<float>> inactive_buffer;
    // pipeline analysis
    std::vector<std::complex<float>>    pipeline_fft_buffer;
    std::vector<std::complex<float>>    pipeline_dqpsk_vec_buffer;
    std::vector<float>                  pipeline_dqpsk_buffer;
    std::vector<viterbi_bit_t>          pipeline_out_bits;
    std::vector<float>                  pipeline_fft_mag_buffer;
    // prs correlation buffer
    CircularBuffer<std::complex<float>> null_power_dip_buffer;
    ReconstructionBuffer<std::complex<float>> correlation_time_buffer;
    std::vector<float>                  correlation_impulse_response;
    std::vector<std::complex<float>>    correlation_fft_buffer;
    std::vector<std::complex<float>>    correlation_prs_fft_reference;
    // fft
    kiss_fft_cfg fft_cfg;
    kiss_fft_cfg ifft_cfg;
    // threads
    std::unique_ptr<OFDM_Demod_Coordinator_Thread>           coordinator_thread;
    std::vector<std::unique_ptr<OFDM_Demod_Pipeline_Thread>> pipelines;
    std::vector<std::unique_ptr<std::thread>>                threads;
    // callback for when ofdm is completed
    Observable<tcb::span<const viterbi_bit_t>> obs_on_ofdm_frame;
public:
    OFDM_Demod(
        const OFDM_Params _params, 
        tcb::span<const std::complex<float>> _prs_fft_ref, 
        tcb::span<const int> _carrier_mapper);
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
    State GetState() const { return state; }
    float GetFineFrequencyOffset() const { return freq_fine_offset; }
    size_t Get_OFDM_Frame_Total_Bits() const { 
        return params.nb_data_carriers*2*(params.nb_frame_symbols-1); 
    }
    int GetTotalFramesRead() const { return total_frames_read; }
    int GetTotalFramesDesync() const { return total_frames_desync; }
    OFDM_Params GetOFDMParams() const { return params; }
    tcb::span<std::complex<float>> GetFrameDataVec() { return pipeline_dqpsk_vec_buffer; }
    tcb::span<float> GetFrameDataPhases() { return pipeline_dqpsk_buffer; }
    tcb::span<float> GetImpulseResponse() { return correlation_impulse_response; }
    float GetSignalAverage() const { return signal_l1_average; }
    auto& GetConfig() { return cfg; }
    auto& On_OFDM_Frame() { return obs_on_ofdm_frame; }
private:
    void CoordinatorThread();
    void PipelineThread(OFDM_Demod_Pipeline_Thread& thread_data, OFDM_Demod_Pipeline_Thread* dependent_thread_data);
private:
    size_t FindNullPowerDip(tcb::span<const std::complex<float>> buf);
    size_t FindPRSCorrelation(tcb::span<const std::complex<float>> buf);
    size_t FillBuffer(tcb::span<const std::complex<float>> buf);
private:
    float ApplyPLL(tcb::span<const std::complex<float>> x, tcb::span<std::complex<float>> y, const float dt0=0);
    float CalculateTimeOffset(const size_t i);
    float CalculateCyclicPhaseError(tcb::span<const std::complex<float>> sym);
    void CalculateMagnitude(tcb::span<const std::complex<float>> fft_buf, tcb::span<float> mag_buf);
    void CalculateDQPSK(
        tcb::span<const std::complex<float>> in0, tcb::span<const std::complex<float>> in1, 
        tcb::span<std::complex<float>> out_vec, tcb::span<float> out_phase);
    void CalculateViterbiBits(tcb::span<const float> phase_buf, tcb::span<viterbi_bit_t> bit_buf);
    float CalculateL1Average(tcb::span<const std::complex<float>> block);
    void UpdateSignalAverage(tcb::span<const std::complex<float>> block);
    void UpdateFineFrequencyOffset(const float cyclic_error);
};

