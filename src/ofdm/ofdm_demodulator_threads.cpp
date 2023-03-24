#include "./ofdm_demodulator_threads.h"
#include <stdint.h>

#define PROFILE_ENABLE 1
#include "utility/profiler.h"

// Pipeline thread
OFDM_Demod_Pipeline_Thread::OFDM_Demod_Pipeline_Thread(const size_t _start, const size_t _end) 
: symbol_start(_start), symbol_end(_end)
{
    is_start = false;
    is_phase_error_done = false;
    is_fft_done = false;
    is_end = false;
    is_terminated = false;
}

OFDM_Demod_Pipeline_Thread::~OFDM_Demod_Pipeline_Thread() {
    Stop();
}

void OFDM_Demod_Pipeline_Thread::Stop() {
    PROFILE_BEGIN_FUNC();
    is_terminated = true;
    Start();
}

void OFDM_Demod_Pipeline_Thread::Start() {
    PROFILE_BEGIN_FUNC();

    PROFILE_BEGIN(lock_create);
    auto lock = std::scoped_lock(mutex_start);
    PROFILE_END(lock_create);

    is_start = true;
    PROFILE_BEGIN(cv_notify);
    cv_start.notify_one();
}

void OFDM_Demod_Pipeline_Thread::WaitStart() {
    PROFILE_BEGIN_FUNC();
    if (is_terminated) return;

    PROFILE_BEGIN(lock_create);
    auto lock = std::unique_lock(mutex_start);
    PROFILE_END(lock_create);

    PROFILE_BEGIN(cv_wait);
    cv_start.wait(lock, [this]() { return is_start; });
    is_start = false;
}

void OFDM_Demod_Pipeline_Thread::SignalPhaseError() {
    PROFILE_BEGIN_FUNC();
    auto lock = std::scoped_lock(mutex_phase_error_done);
    is_phase_error_done = true;
    cv_phase_error_done.notify_one();
}

void OFDM_Demod_Pipeline_Thread::WaitPhaseError() {
    auto lock = std::unique_lock(mutex_phase_error_done);
    cv_phase_error_done.wait(lock, [this]() { return is_phase_error_done; });
    is_phase_error_done = false;
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

void OFDM_Demod_Pipeline_Thread::SignalEnd() {
    PROFILE_BEGIN_FUNC();

    PROFILE_BEGIN(lock_create);
    auto lock = std::scoped_lock(mutex_end);
    PROFILE_END(lock_create);

    is_end = true;
    PROFILE_BEGIN(cv_notify);
    cv_end.notify_one();
}

void OFDM_Demod_Pipeline_Thread::WaitEnd() {
    PROFILE_BEGIN_FUNC();

    PROFILE_BEGIN(lock_create);
    auto lock = std::unique_lock(mutex_end);
    PROFILE_END(lock_create);

    PROFILE_BEGIN(cv_wait);
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