#pragma once

#include <vector>
#include <mutex>
#include <condition_variable>

template <typename T>
class DoubleBuffer 
{
private:
    std::vector<T> active_buffer;
    std::vector<T> inactive_buffer;
    size_t length; 

    bool is_start_buffer;
    std::mutex mutex_start_buffer;
    std::condition_variable cv_start_buffer;

    bool is_end_buffer;
    std::mutex mutex_end_buffer;
    std::condition_variable cv_end_buffer;

    bool is_send_terminate;
public:
    DoubleBuffer(const size_t _length)
    : length(_length),
      active_buffer(_length), inactive_buffer(_length)
    {
        is_start_buffer = false;
        is_end_buffer = false;
        is_send_terminate = false;
        ReleaseActiveBuffer();
    }
    ~DoubleBuffer() {
        Close();
    }
    DoubleBuffer(DoubleBuffer&) = delete;
    DoubleBuffer(DoubleBuffer&&) = delete;
    DoubleBuffer& operator=(DoubleBuffer&) = delete;
    DoubleBuffer& operator=(DoubleBuffer&&) = delete;
    size_t GetLength(void) const { 
        return length; 
    }
    void Close(void) {
        is_send_terminate = true;
        SignalStartBuffer();
        SignalEndBuffer();
    }
    T* AcquireInactiveBuffer(void) {
        if (is_send_terminate) {
            return NULL;
        }
        return inactive_buffer.data();
    }
    void ReleaseInactiveBuffer(void) {
        WaitEndBuffer();
        std::swap(inactive_buffer, active_buffer);
        SignalStartBuffer();
    }
    T* AcquireActiveBuffer(void) {
        WaitStartBuffer();
        if (is_send_terminate) {
            return NULL;
        }
        return active_buffer.data();
    }
    void ReleaseActiveBuffer(void) {
        SignalEndBuffer();
    }
private:
    void SignalStartBuffer() {
        auto lock = std::scoped_lock(mutex_start_buffer);
        is_start_buffer = true;
        cv_start_buffer.notify_one();
    }
    void WaitStartBuffer() {
        if (is_send_terminate) {
            return;
        }
        auto lock = std::unique_lock(mutex_start_buffer);
        cv_start_buffer.wait(lock, [this]() { return is_start_buffer; });
        is_start_buffer = false;
    }
    void SignalEndBuffer() {
        auto lock = std::scoped_lock(mutex_end_buffer);
        is_end_buffer = true;
        cv_end_buffer.notify_one();
    }
    void WaitEndBuffer() {
        if (is_send_terminate) {
            return;
        }
        auto lock = std::unique_lock(mutex_end_buffer);
        cv_end_buffer.wait(lock, [this]() { return is_end_buffer; });
        is_end_buffer = false;
    }
};
