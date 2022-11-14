#include "ofdm_demodulator_threads.h"
#include <stdint.h>

// Pipeline thread
OFDM_Demod_Pipeline_Thread::OFDM_Demod_Pipeline_Thread(const size_t _start, const size_t _end) 
: symbol_start(_start), symbol_end(_end)
{
    is_start = false;
    is_fft_done = false;
    is_start_dqpsk = false;
    is_end = false;
    is_terminated = false;
}

OFDM_Demod_Pipeline_Thread::~OFDM_Demod_Pipeline_Thread() {
    Stop();
}

void OFDM_Demod_Pipeline_Thread::Stop() {
    is_terminated = true;
    Start();
}

void OFDM_Demod_Pipeline_Thread::Start() {
    auto lock = std::scoped_lock(mutex_start);
    is_start = true;
    cv_start.notify_one();
}

void OFDM_Demod_Pipeline_Thread::WaitStart() {
    if (is_terminated) return;
    auto lock = std::unique_lock(mutex_start);
    cv_start.wait(lock, [this]() { return is_start; });
    is_start = false;
}

void OFDM_Demod_Pipeline_Thread::SignalFFT() {
    auto lock = std::scoped_lock(mutex_fft_done);
    is_fft_done = true;
    cv_fft_done.notify_one();
}

void OFDM_Demod_Pipeline_Thread::WaitFFT() {
    auto lock = std::unique_lock(mutex_fft_done);
    cv_fft_done.wait(lock, [this]() { return is_fft_done; });
    is_fft_done = false;
}

void OFDM_Demod_Pipeline_Thread::StartDQPSK() {
    auto lock = std::scoped_lock(mutex_start_dqpsk);
    is_start_dqpsk = true;
    cv_start_dqpsk.notify_one();
}

void OFDM_Demod_Pipeline_Thread::WaitDQPSK() {
    auto lock = std::unique_lock(mutex_start_dqpsk);
    cv_start_dqpsk.wait(lock, [this]() { return is_start_dqpsk; });
    is_start_dqpsk = false;
}

void OFDM_Demod_Pipeline_Thread::SignalEnd() {
    auto lock = std::scoped_lock(mutex_end);
    is_end = true;
    cv_end.notify_one();
}

void OFDM_Demod_Pipeline_Thread::WaitEnd() {
    auto lock = std::unique_lock(mutex_end);
    cv_end.wait(lock, [this]() { return is_end; });
    is_end = false;
}

// Coordinator thread
OFDM_Demod_Coordinator_Thread::OFDM_Demod_Coordinator_Thread() {
    is_start = false;
    is_end = true;
    is_terminated = false;
    cv_end.notify_all();
}

OFDM_Demod_Coordinator_Thread::~OFDM_Demod_Coordinator_Thread() {
    Stop();
}

void OFDM_Demod_Coordinator_Thread::Stop() {
    is_terminated = true;
    Start();
}

void OFDM_Demod_Coordinator_Thread::Start() {
    auto lock = std::scoped_lock(mutex_start);
    is_start = true;
    cv_start.notify_one();
}

void OFDM_Demod_Coordinator_Thread::WaitStart() {
    if (is_terminated) return;
    auto lock = std::unique_lock(mutex_start);
    cv_start.wait(lock, [this]() { return is_start; });
    is_start = false;
}

void OFDM_Demod_Coordinator_Thread::SignalEnd() {
    auto lock = std::scoped_lock(mutex_end);
    is_end = true;
    cv_end.notify_one();
}

void OFDM_Demod_Coordinator_Thread::Wait() {
    auto lock = std::unique_lock(mutex_end);
    cv_end.wait(lock, [this]() { return is_end; });
    is_end = false;
}