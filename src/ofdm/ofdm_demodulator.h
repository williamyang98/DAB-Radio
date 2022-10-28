#pragma once

#include <stdint.h>

#include <complex>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <memory>

#include "ofdm_params.h"
#include "circular_buffer.h"
#include "reconstruction_buffer.h"
#include "../observable.h"

typedef int16_t viterbi_bit_t;
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
    const int* carrier_mapper;
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
    ReconstructionBuffer<std::complex<float>>* active_buffer;
    ReconstructionBuffer<std::complex<float>>* inactive_buffer;
    // pipeline analysis
    std::complex<float>* pipeline_fft_buffer;
    std::complex<float>* pipeline_dqpsk_vec_buffer;
    float*               pipeline_dqpsk_buffer;
    viterbi_bit_t*       pipeline_out_bits;
    float*               pipeline_fft_mag_buffer;
    // prs correlation buffer
    CircularBuffer<std::complex<float>> null_power_dip_buffer;
    ReconstructionBuffer<std::complex<float>> correlation_time_buffer;
    float* correlation_impulse_response;
    std::complex<float>* correlation_fft_buffer;
    const std::complex<float>* correlation_prs_fft_reference;
    // fft
    kiss_fft_cfg fft_cfg;
    kiss_fft_cfg ifft_cfg;
    // threads
    OFDM_Demod_Coordinator_Thread* coordinator_thread;
    std::vector<std::unique_ptr<OFDM_Demod_Pipeline_Thread>> pipelines;
    std::vector<std::unique_ptr<std::thread>> threads;
    // callback for when ofdm is completed
    Observable<const viterbi_bit_t*, const int, const int> obs_on_ofdm_frame;
public:
    OFDM_Demod(const OFDM_Params _params, const std::complex<float>* _prs_fft_ref, const int* _carrier_mapper);
    ~OFDM_Demod();
    void Process(const std::complex<float>* block, const int N);
public:
    inline State GetState(void) const { return state; }
    inline float GetFineFrequencyOffset(void) const { return freq_fine_offset; }
    inline int Get_OFDM_Frame_Total_Bits(void) const { return params.nb_data_carriers*2*(params.nb_frame_symbols-1); }
    inline int GetTotalFramesRead(void) const { return total_frames_read; }
    inline int GetTotalFramesDesync(void) const { return total_frames_desync; }
    inline OFDM_Params GetOFDMParams(void) const { return params; }
    inline std::complex<float>* GetFrameDataVec(void) { return pipeline_dqpsk_vec_buffer; }
    inline float* GetFrameDataPhases(void) { return pipeline_dqpsk_buffer; }
    inline float* GetImpulseResponse(void) { return correlation_impulse_response; }
    inline float GetSignalAverage(void) const { return signal_l1_average; }
    inline auto& GetConfig(void) { return cfg; }
    inline void SetConfig(const Config _cfg) { cfg = _cfg; }
    inline auto& On_OFDM_Frame(void) { return obs_on_ofdm_frame; }
private:
    void CoordinatorThread();
    void PipelineThread(OFDM_Demod_Pipeline_Thread* thread_data, OFDM_Demod_Pipeline_Thread* dependent_thread_data);
private:
    int FindNullPowerDip(const std::complex<float>* buf, const int N);
    int FindPRSCorrelation(const std::complex<float>* buf, const int N);
    int FillBuffer(const std::complex<float>* buf, const int N);
private:
    float ApplyPLL(
        const std::complex<float>* x, std::complex<float>* y, 
        const int N, const float dt0);
    float CalculateTimeOffset(const int i);
    float CalculateCyclicPhaseError(const std::complex<float>* sym);
    void CalculateMagnitude(const std::complex<float>* fft_buf, float* mag_buf);
    void CalculateDQPSK(const std::complex<float>* in0, const std::complex<float>* in1, std::complex<float>* out_vec, float* out_phase);
    void CalculateViterbiBits(const float* phase_buf, viterbi_bit_t* bit_buf);
    float CalculateL1Average(const std::complex<float>* block, const int N);
    void UpdateSignalAverage(const std::complex<float>* block, const int N);
    void UpdateFineFrequencyOffset(const float cyclic_error);
};

