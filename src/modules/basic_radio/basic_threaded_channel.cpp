#include "basic_threaded_channel.h"

// Base class for threaded channel
BasicThreadedChannel::BasicThreadedChannel() {
    is_start = false;
    is_join = false;
    is_running = true;
    is_terminated = false;
    runner_thread = new std::thread([this]() {
        RunnerThread();
    });
}

BasicThreadedChannel::~BasicThreadedChannel() {
    Stop();
    Join();
    runner_thread->join();
    delete runner_thread;
}

void BasicThreadedChannel::Start() {
    auto lock = std::scoped_lock(mutex_start);
    is_start = true;
    cv_start.notify_all();
}

void BasicThreadedChannel::Join() {
    // Wait for complete termination
    if (!is_running) {
        if (is_terminated) {
            return;
        }
        
        auto lock = std::unique_lock(mutex_terminate);
        cv_terminate.wait(lock, [this]() { return is_terminated; });
        return;
    }
    auto lock = std::unique_lock(mutex_join);
    cv_join.wait(lock, [this]() { return is_join; });
    is_join = false;
}

void BasicThreadedChannel::Stop() {
    if (!is_running) {
        return;
    }
    is_running = false;
    Start();
}

void BasicThreadedChannel::RunnerThread() {
    BeforeRun();
    while (is_running) {
        {
            auto lock = std::unique_lock(mutex_start);
            cv_start.wait(lock, [this]() { return is_start; });
            is_start = false;
        }
        if (!is_running) {
            auto lock = std::scoped_lock(mutex_join);
            is_join = true;
            cv_join.notify_all();
            break;
        }
        Run();
        {
            auto lock = std::scoped_lock(mutex_join);
            is_join = true;
            cv_join.notify_all();
        }
    }

    auto lock = std::scoped_lock(mutex_terminate);
    is_terminated = true;
    cv_terminate.notify_all();
}