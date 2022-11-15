#pragma once

#include <mutex>
#include <condition_variable>

// Helper classes to manage synchronisation between the worker threads
// in our multithreaded OFDM to frame bits demodulator
// We have an coordinator thread to synchronise our pipeline processing threads

class OFDM_Demod_Pipeline_Thread 
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
    OFDM_Demod_Pipeline_Thread(const size_t _start, const size_t _end);
    ~OFDM_Demod_Pipeline_Thread();
    // This thread contains mutexes which we do not intend to copy/move
    OFDM_Demod_Pipeline_Thread(OFDM_Demod_Pipeline_Thread&) = delete;
    OFDM_Demod_Pipeline_Thread(OFDM_Demod_Pipeline_Thread&&) = delete;
    OFDM_Demod_Pipeline_Thread& operator=(OFDM_Demod_Pipeline_Thread&) = delete;
    OFDM_Demod_Pipeline_Thread& operator=(OFDM_Demod_Pipeline_Thread&&) = delete;
    size_t GetSymbolStart() const { return symbol_start; }
    size_t GetSymbolEnd() const { return symbol_end; }
    inline float& GetAveragePhaseError() { return average_phase_error; }
    void Stop();
    bool IsStopped() const { return is_terminated; }
    // Called from coordinator thread
    void Start();
    void WaitPhaseError();
    void WaitEnd();
    // Called by pipeline thread
    void WaitStart();
    void SignalPhaseError();
    void SignalFFT();
    void WaitFFT();
    void SignalEnd();
};

class OFDM_Demod_Coordinator_Thread 
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
    OFDM_Demod_Coordinator_Thread();
    ~OFDM_Demod_Coordinator_Thread();
    // This thread contains mutexes which we do not intend to copy/move
    OFDM_Demod_Coordinator_Thread(OFDM_Demod_Coordinator_Thread&) = delete;
    OFDM_Demod_Coordinator_Thread(OFDM_Demod_Coordinator_Thread&&) = delete;
    OFDM_Demod_Coordinator_Thread& operator=(OFDM_Demod_Coordinator_Thread&) = delete;
    OFDM_Demod_Coordinator_Thread& operator=(OFDM_Demod_Coordinator_Thread&&) = delete;
    void Stop();
    bool IsStopped() const { return is_terminated; }
    // Called by reader thread
    void Start();
    void Wait();
    // Called by coordinator thread
    void WaitStart();
    void SignalEnd();
};
