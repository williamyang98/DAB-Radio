#pragma once
#include <complex>
#include <stdint.h>

#include "ofdm_params.h"
#include "reconstruction_buffer.h"
#include "circular_buffer.h"
#include "../observable.h"

// forward declare kissfft config
typedef struct kiss_fft_state* kiss_fft_cfg;

class OFDM_Demodulator 
{
public:
    struct Config {
        struct {
            float update_beta = 0.95f;
            int nb_samples = 100;
            int nb_decimate = 5;
        } signal_l1;
        float fine_freq_update_beta = 0.1f;
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
        WAITING_NULL,
        READING_OFDM_FRAME,
        READING_NULL_SYMBOL
    };
private:
    const struct OFDM_Params params;
    Config cfg;
    kiss_fft_cfg fft_cfg;
    State state;

    // statistics
    int total_frames_read;
    int total_frames_desync;

    // ofdm frequency correction
    float freq_fine_offset;
    float freq_dt;
    bool is_update_fine_freq;

    // ofdm symbol processing
    ReconstructionBuffer<std::complex<float>>* ofdm_sym_wrap_buf;
    std::complex<float>* ofdm_sym_pll_buf;
    // additional buffer used for storing previous symbol for dqpsk
    int curr_ofdm_symbol;
    std::complex<float>* curr_sym_fft_buf;
    std::complex<float>* last_sym_fft_buf;
    // subcarrier phases for each ofdm symbol
    float* ofdm_frame_raw;
    uint8_t* ofdm_frame_pred;
    // store the average spectrum of ofdm symbols
    float* ofdm_magnitude_avg;

    // null symbol processing
    ReconstructionBuffer<std::complex<float>>* null_sym_wrap_buf;
    std::complex<float>* null_sym_pll_buf;
    // fft buffer for extracing TII (transmitter identification information)
    std::complex<float>* null_sym_fft_buf;
    bool is_read_null_symbol;
    float* null_sym_data;

    // find start of PRS using null power dip
    bool is_null_start_found;
    bool is_null_end_found;
    float signal_l1_average;

    // fine time synchronisation to start of PRS using impulse response correlation
    bool is_found_prs;
    CircularBuffer<std::complex<float>>* null_search_buf;
    int null_search_prs_index;
    ReconstructionBuffer<std::complex<float>>* null_prs_linearise_buf;
    std::complex<float>* prs_fft_reference;
    std::complex<float>* prs_fft_actual;
    float* prs_impulse_response;
private:
    // callback for when ofdm is completed
    Observable<const uint8_t*, const int, const int> obs_on_ofdm_frame;
public:
    OFDM_Demodulator(
        const struct OFDM_Params _ofdm_params,
        const std::complex<float>* _ofdm_prs_ref);
    ~OFDM_Demodulator();
    void ProcessBlock(std::complex<float>* block, const int N);
    inline State GetState(void) const { return state; }
    inline float GetFineFrequencyOffset(void) const { return freq_fine_offset; }
    inline int GetTotalFramesRead(void) const { return total_frames_read; }
    inline int GetTotalFramesDesync(void) const { return total_frames_desync; }
    inline OFDM_Params GetOFDMParams(void) const { return params; }
    inline bool& GetIsUpdateFineFrequency(void) { return is_update_fine_freq; }

    inline float* GetNullSymbolMagnitude(void) { return null_sym_data; }
    inline float* GetFrameMagnitudeAverage(void) { return ofdm_magnitude_avg; }
    inline float* GetFrameDataPhases(void) { return ofdm_frame_raw; }
    inline uint8_t* GetFrameDataPhasesPred(void) { return ofdm_frame_pred; }
    inline float* GetImpulseResponse(void) { return prs_impulse_response; }

    inline float GetSignalAverage(void) const { return signal_l1_average; }
    inline int GetCurrentOFDMSymbol(void) const { return curr_ofdm_symbol; }
    
    inline auto& GetConfig(void) { return cfg; }
    inline void SetConfig(const Config _cfg) { cfg = _cfg; }

    inline auto& On_OFDM_Frame(void) { return obs_on_ofdm_frame; }
private:
    void ProcessBlockWithoutUpdate(std::complex<float>* block, const int N);

    int FindNullSync(std::complex<float>* block, const int N);
    int FindNullSync_Power(std::complex<float>* block, const int N);
    int FindNullSync_Correlation(std::complex<float>* block, const int N);

    int ReadOFDMSymbols(std::complex<float>* block, const int N);
    void ProcessOFDMSymbol(std::complex<float>* sym);
    float CalculateCyclicPhaseError(const std::complex<float>* sym);
    void UpdateMagnitudeAverage(std::complex<float>* Y);

    int ReadNullSymbol(std::complex<float>* block, const int N);
    void ProcessNullSymbol(std::complex<float>* sym);

    void UpdateSignalAverage(std::complex<float>* block, const int N);
    float CalculateL1Average(std::complex<float>* block, const int N);
    float ApplyPLL(
        const std::complex<float>* x, std::complex<float>* y, 
        const int N, const float dt0=0.0f);
};