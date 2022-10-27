#pragma once

#include <mutex>
#include <condition_variable>

// Helper classes to manage synchronisation between the worker threads
// in our multithreaded OFDM to frame bits demodulator
// We have an coordinator thread to synchronise our pipeline processing threads

class OFDM_Demod_Pipeline_Thread 
{
private:
    const int symbol_start;
    const int symbol_end;
    float average_phase_error;

    bool is_start;
    std::mutex mutex_start;
    std::condition_variable cv_start;

    bool is_fft_done;
    std::mutex mutex_fft_done;
    std::condition_variable cv_fft_done;

    bool is_start_dqpsk;
    std::mutex mutex_start_dqpsk;
    std::condition_variable cv_start_dqpsk;

    bool is_end;
    std::mutex mutex_end;
    std::condition_variable cv_end;
public:
    OFDM_Demod_Pipeline_Thread(const int _start, const int _end);
    ~OFDM_Demod_Pipeline_Thread();
    inline int GetSymbolStart(void) const { return symbol_start; }
    inline int GetSymbolEnd(void) const { return symbol_end; }
    inline float& GetAveragePhaseError(void) { return average_phase_error; }
// reader thread
    void Start();
    void WaitFFT();
    void StartDQPSK();
    void WaitEnd();
// worker thread
    void WaitStart();
    void SignalFFT();
    void WaitDQPSK();
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
public:
    OFDM_Demod_Coordinator_Thread();
    ~OFDM_Demod_Coordinator_Thread();
// reader thread
    void Start();
    void Wait();
// worker thread
    void WaitStart();
    void SignalEnd();
};
