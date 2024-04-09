#pragma once

#include <stddef.h>
#include <condition_variable>
#include <mutex>

// Helper classes to manage synchronisation between the OFDM demodulator pipeline threads
// We have an coordinator thread to synchronise our pipeline threads

class OFDM_Demod_Pipeline 
{
private:
    const size_t m_symbol_start;
    const size_t m_symbol_end;
    float m_average_phase_error;

    bool m_is_start;
    std::mutex m_mutex_start;
    std::condition_variable m_cv_start;

    bool m_is_phase_error_done;
    std::mutex m_mutex_phase_error_done;
    std::condition_variable m_cv_phase_error_done;

    bool m_is_fft_done;
    std::mutex m_mutex_fft_done;
    std::condition_variable m_cv_fft_done;

    bool m_is_end;
    std::mutex m_mutex_end;
    std::condition_variable m_cv_end;

    bool m_is_terminated;
public:
    OFDM_Demod_Pipeline(const size_t start, const size_t end);
    ~OFDM_Demod_Pipeline();
    // This thread contains mutexes which we do not intend to copy/move
    OFDM_Demod_Pipeline(OFDM_Demod_Pipeline&) = delete;
    OFDM_Demod_Pipeline(OFDM_Demod_Pipeline&&) = delete;
    OFDM_Demod_Pipeline& operator=(OFDM_Demod_Pipeline&) = delete;
    OFDM_Demod_Pipeline& operator=(OFDM_Demod_Pipeline&&) = delete;
    size_t GetSymbolStart() const { return m_symbol_start; }
    size_t GetSymbolEnd() const { return m_symbol_end; }
    float GetAveragePhaseError() const { return m_average_phase_error; }
    void SetAveragePhaseError(const float error) { m_average_phase_error = error; }
    void Stop();
    bool IsStopped() const { return m_is_terminated; }
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
    bool m_is_start;
    std::mutex m_mutex_start;
    std::condition_variable m_cv_start;

    bool m_is_end;
    std::mutex m_mutex_end;
    std::condition_variable m_cv_end;

    bool m_is_terminated;
public:
    OFDM_Demod_Coordinator();
    ~OFDM_Demod_Coordinator();
    // This thread contains mutexes which we do not intend to copy/move
    OFDM_Demod_Coordinator(OFDM_Demod_Coordinator&) = delete;
    OFDM_Demod_Coordinator(OFDM_Demod_Coordinator&&) = delete;
    OFDM_Demod_Coordinator& operator=(OFDM_Demod_Coordinator&) = delete;
    OFDM_Demod_Coordinator& operator=(OFDM_Demod_Coordinator&&) = delete;
    void Stop();
    bool IsStopped() const { return m_is_terminated; }
    // Called by reader thread
    void SignalStart();
    void WaitEnd();
    // Called by coordinator thread
    // NOTE: WaitStart() exits early if the thread was terminated
    //       This needs to be checked by the waiting thread using IsStopped()
    void WaitStart();
    void SignalEnd();
};
