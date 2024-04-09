#include "./ofdm_demodulator_threads.h"
#include <stddef.h>
#include <mutex>

#define PROFILE_ENABLE 1
#include "./profiler.h"

// Pipeline thread
OFDM_Demod_Pipeline::OFDM_Demod_Pipeline(const size_t start, const size_t end) 
: m_symbol_start(start), m_symbol_end(end)
{
    m_is_start = false;
    m_is_phase_error_done = false;
    m_is_fft_done = false;
    m_is_end = false;
    m_is_terminated = false;
    m_average_phase_error = 0.0f;
}

OFDM_Demod_Pipeline::~OFDM_Demod_Pipeline() {
    Stop();
}

void OFDM_Demod_Pipeline::Stop() {
    PROFILE_BEGIN_FUNC();
    m_is_terminated = true;
    SignalStart();
}

void OFDM_Demod_Pipeline::SignalStart() {
    PROFILE_BEGIN_FUNC();

    PROFILE_BEGIN(lock_create);
    auto lock = std::scoped_lock(m_mutex_start);
    PROFILE_END(lock_create);

    m_is_start = true;
    PROFILE_BEGIN(cv_notify);
    m_cv_start.notify_one();
}

void OFDM_Demod_Pipeline::WaitStart() {
    PROFILE_BEGIN_FUNC();
    if (m_is_terminated) return;

    PROFILE_BEGIN(lock_create);
    auto lock = std::unique_lock(m_mutex_start);
    PROFILE_END(lock_create);

    PROFILE_BEGIN(cv_wait);
    m_cv_start.wait(lock, [this]() { return m_is_start; });
    m_is_start = false;
}

void OFDM_Demod_Pipeline::SignalPhaseError() {
    PROFILE_BEGIN_FUNC();
    auto lock = std::scoped_lock(m_mutex_phase_error_done);
    m_is_phase_error_done = true;
    m_cv_phase_error_done.notify_one();
}

void OFDM_Demod_Pipeline::WaitPhaseError() {
    auto lock = std::unique_lock(m_mutex_phase_error_done);
    m_cv_phase_error_done.wait(lock, [this]() { return m_is_phase_error_done; });
    m_is_phase_error_done = false;
}

void OFDM_Demod_Pipeline::SignalFFT() {
    auto lock = std::scoped_lock(m_mutex_fft_done);
    m_is_fft_done = true;
    m_cv_fft_done.notify_one();
}

void OFDM_Demod_Pipeline::WaitFFT() {
    auto lock = std::unique_lock(m_mutex_fft_done);
    m_cv_fft_done.wait(lock, [this]() { return m_is_fft_done; });
    m_is_fft_done = false;
}

void OFDM_Demod_Pipeline::SignalEnd() {
    PROFILE_BEGIN_FUNC();

    PROFILE_BEGIN(lock_create);
    auto lock = std::scoped_lock(m_mutex_end);
    PROFILE_END(lock_create);

    m_is_end = true;
    PROFILE_BEGIN(cv_notify);
    m_cv_end.notify_one();
}

void OFDM_Demod_Pipeline::WaitEnd() {
    PROFILE_BEGIN_FUNC();

    PROFILE_BEGIN(lock_create);
    auto lock = std::unique_lock(m_mutex_end);
    PROFILE_END(lock_create);

    PROFILE_BEGIN(cv_wait);
    m_cv_end.wait(lock, [this]() { return m_is_end; });
    m_is_end = false;
}

// Coordinator thread
OFDM_Demod_Coordinator::OFDM_Demod_Coordinator() {
    m_is_start = false;
    m_is_end = true;
    m_is_terminated = false;
    m_cv_end.notify_all();
}

OFDM_Demod_Coordinator::~OFDM_Demod_Coordinator() {
    Stop();
}

void OFDM_Demod_Coordinator::Stop() {
    m_is_terminated = true;
    SignalStart();
}

void OFDM_Demod_Coordinator::SignalStart() {
    auto lock = std::scoped_lock(m_mutex_start);
    m_is_start = true;
    m_cv_start.notify_one();
}

void OFDM_Demod_Coordinator::WaitStart() {
    if (m_is_terminated) return;
    auto lock = std::unique_lock(m_mutex_start);
    m_cv_start.wait(lock, [this]() { return m_is_start; });
    m_is_start = false;
}

void OFDM_Demod_Coordinator::SignalEnd() {
    auto lock = std::scoped_lock(m_mutex_end);
    m_is_end = true;
    m_cv_end.notify_one();
}

void OFDM_Demod_Coordinator::WaitEnd() {
    auto lock = std::unique_lock(m_mutex_end);
    m_cv_end.wait(lock, [this]() { return m_is_end; });
    m_is_end = false;
}