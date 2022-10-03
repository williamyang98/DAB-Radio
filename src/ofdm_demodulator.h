#pragma once
#include <complex>
#include <stdint.h>

#include "ofdm_params.h"
#include "reconstruction_buffer.h"
#include "circular_buffer.h"

// forward declare kissfft config
typedef struct kiss_fft_state* kiss_fft_cfg;

class OFDM_Demodulator 
{
public:
    enum State {
        WAITING_NULL,
        READING_OFDM_FRAME,
        READING_NULL_SYMBOL
    };
    struct ProcessResult {
        bool is_processing;
        bool is_sync_found;
        int sync_index;
        bool is_ofdm_finished;
        int ofdm_end_index;
    };
public:
    // parameters of the OFDM signal
    const struct OFDM_Params params;
    // parameters of the OFDM demodulator
    struct {
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
    } cfg;

    kiss_fft_cfg fft_cfg;
    State state;
    // statistics
    int total_frames_read;
    int total_frames_desync;
    // store the average spectrum of ofdm symbols
    float* ofdm_magnitude_avg;

    // ofdm frequency correction
    float freq_fine_offset;
    float freq_dt;
    bool is_update_fine_freq;

    // ofdm symbol processing
    ReconstructionBuffer<std::complex<float>>* ofdm_sym_wrap_buf;
    // pll for ofdm
    std::complex<float>* ofdm_sym_pll_buf;
    // fft buffers for processing ofdm symbols
    // additional buffer used for storing previous symbol for dqpsk
    std::complex<float>* curr_sym_fft_buf;
    std::complex<float>* last_sym_fft_buf;
    int curr_ofdm_symbol;
    float* ofdm_frame_data;
    uint8_t* ofdm_frame_bits;

    // null symbol processing
    ReconstructionBuffer<std::complex<float>>* null_sym_wrap_buf;
    // pll for null symbol
    std::complex<float>* null_sym_pll_buf;
    // fft buffer for extracing TII (transmitter identification information)
    std::complex<float>* null_sym_fft_buf;
    bool is_read_null_symbol;
    float* null_sym_data;

    // null detection
    bool is_found_prs;
    // store samples for backtrack in circular buffer
    // we might find that our inital estimate of null symbol end if off
    // if it is ahead of the actual null symbol end, then we need to backtrack
    CircularBuffer<std::complex<float>>* null_search_buf;
    int null_search_prs_index;
    // linearise circular buffer to store PRS
    ReconstructionBuffer<std::complex<float>>* null_prs_linearise_buf;
    // fine time frame synchronisation using PRS
    std::complex<float>* prs_fft_reference;
    std::complex<float>* prs_fft_actual;
    float* prs_impulse_response;
    // method 1: ofdm frame end then correlation
    // method 2: null power detection then correlation
    //   this method is only run if null_seach.prs_index = -1
    bool is_null_start_found;
    bool is_null_end_found;
    float signal_l1_average;


public:
    OFDM_Demodulator(
        const struct OFDM_Params _ofdm_params,
        const std::complex<float>* _ofdm_prs_ref);
    ~OFDM_Demodulator();
    void ProcessBlock(std::complex<float>* block, const int N);
    inline State GetState(void) { return state; }
private:
    void ProcessBlockWithoutUpdate(std::complex<float>* block, const int N);
    void UpdateSignalAverage(std::complex<float>* block, const int N);
    int FindNullSync(std::complex<float>* block, const int N);
    int ReadOFDMSymbols(std::complex<float>* block, const int N);
    void ProcessOFDMSymbol(std::complex<float>* sym);
    int ReadNullSymbol(std::complex<float>* block, const int N);
    void ProcessNullSymbol(std::complex<float>* sym);
    float CalculateL1Average(std::complex<float>* block, const int N);
    void UpdateMagnitudeAverage(std::complex<float>* Y);
    float ApplyPLL(
        const std::complex<float>* x, std::complex<float>* y, 
        const int N, const float dt0=0.0f);
};