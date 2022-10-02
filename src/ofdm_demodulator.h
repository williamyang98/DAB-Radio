#pragma once
#include <complex>

#include "ofdm_params.h"

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
    const struct OFDM_Params params;
    kiss_fft_cfg fft_cfg;
    State state;

    int total_frames_read;

    // store the average spectrum of ofdm symbols
    float* ofdm_magnitude_avg;

    // ofdm frequency correction
    float freq_fine_offset;
    float freq_dt;
    bool is_update_fine_freq;

    // ofdm symbol processing
    struct {
        std::complex<float>* buf;
        int capacity;
    } ofdm_sym_wrap;
    // pll for ofdm
    std::complex<float>* ofdm_sym_pll_buf;
    // fft buffers for processing ofdm symbols
    // additional buffer used for storing previous symbol for dqpsk
    std::complex<float>* curr_sym_fft_buf;
    std::complex<float>* last_sym_fft_buf;
    int curr_ofdm_symbol;
    float* ofdm_frame_data;

    // null symbol processing
    struct {
        std::complex<float>* buf;
        int capacity;
    } null_sym_wrap;
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
    struct {
        std::complex<float>* buf;
        int length;
        int index;
        int prs_index;
        int capacity;
    } null_search;
    // linearise circular buffer to store PRS
    struct {
        std::complex<float>* buf;
        int length;
        int capacity;
    } null_search_prs;
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
    float signal_l1_beta;
    int signal_l1_nb_samples;


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
};