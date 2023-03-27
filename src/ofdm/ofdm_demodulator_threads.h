#pragma once

#include <mutex>
#include <condition_variable>

// Helper classes to manage synchronisation between the OFDM demodulator pipeline threads
// We have an coordinator thread to synchronise our pipeline threads

class OFDM_Demod_Pipeline 
{
private:
    const size_t symbol_start;
    const size_t symbol_end;
    float average_phase_error;

    bool is_start;
    std::mutex mutex_start;
    std::condition_variable cv_start;

    bool is_phase_error_done;
    std::mutex mutex_phase_error_done;
    std::condition_variable cv_phase_error_done;

    bool is_fft_done;
    std::mutex mutex_fft_done;
    std::condition_variable cv_fft_done;

    bool is_end;
    std::mutex mutex_end;
    std::condition_variable cv_end;

    bool is_terminated;
public:
    OFDM_Demod_Pipeline(const size_t _start, const size_t _end);
    ~OFDM_Demod_Pipeline();
    // This thread contains mutexes which we do not intend to copy/move
    OFDM_Demod_Pipeline(OFDM_Demod_Pipeline&) = delete;
    OFDM_Demod_Pipeline(OFDM_Demod_Pipeline&&) = delete;
    OFDM_Demod_Pipeline& operator=(OFDM_Demod_Pipeline&) = delete;
    OFDM_Demod_Pipeline& operator=(OFDM_Demod_Pipeline&&) = delete;
    size_t GetSymbolStart() const { return symbol_start; }
    size_t GetSymbolEnd() const { return symbol_end; }
    float GetAveragePhaseError() const { return average_phase_error; }
    void SetAveragePhaseError(const float error) { average_phase_error = error; }
    void Stop();
    bool IsStopped() const { return is_terminated; }
    // Called from coordinator thread
    void SignalStart();
    void WaitPhaseError();
    void WaitEnd();
    // Called by pipeline thread
    // NOTE: WaitStart() exits early if the thread was terminated
    //       This needs to be checked by the waiting thread using IsStopped()
    void WaitStart();
    void SignalPhaseError();
    void SignalFFT();
    void WaitFFT();
    void SignalEnd();
};

class OFDM_Demod_Coordinator 
{
private:
    bool is_start;
    std::mutex mutex_start;
    std::condition_variable cv_start;

    bool is_end;
    std::mutex mutex_end;
    std::condition_variable cv_end;

    bool is_terminated;
public:
    OFDM_Demod_Coordinator();
    ~OFDM_Demod_Coordinator();
    // This thread contains mutexes which we do not intend to copy/move
    OFDM_Demod_Coordinator(OFDM_Demod_Coordinator&) = delete;
    OFDM_Demod_Coordinator(OFDM_Demod_Coordinator&&) = delete;
    OFDM_Demod_Coordinator& operator=(OFDM_Demod_Coordinator&) = delete;
    OFDM_Demod_Coordinator& operator=(OFDM_Demod_Coordinator&&) = delete;
    void Stop();
    bool IsStopped() const { return is_terminated; }
    // Called by reader thread
    void SignalStart();
    void WaitEnd();
    // Called by coordinator thread
    // NOTE: WaitStart() exits early if the thread was terminated
    //       This needs to be checked by the waiting thread using IsStopped()
    void WaitStart();
    void SignalEnd();
};
