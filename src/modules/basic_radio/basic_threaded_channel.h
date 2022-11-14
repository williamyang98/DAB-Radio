#pragma once

#include <thread>
#include <memory>
#include <mutex>
#include <condition_variable>

class BasicThreadedChannel 
{
private:
    bool is_running;
    bool is_terminated;
    bool is_start;
    bool is_join;
    std::unique_ptr<std::thread> runner_thread;
    std::mutex mutex_start;
    std::condition_variable cv_start;
    std::mutex mutex_join;
    std::condition_variable cv_join;
    std::mutex mutex_terminate;
    std::condition_variable cv_terminate;
public:
    BasicThreadedChannel();
    virtual ~BasicThreadedChannel();
    // The runner thread takes a lambda 
    // this pointer is passed to it at initialisation, so we cannot move this class 
    BasicThreadedChannel(BasicThreadedChannel&) = delete;
    BasicThreadedChannel(BasicThreadedChannel&&) = delete;
    BasicThreadedChannel& operator=(BasicThreadedChannel&) = delete;
    BasicThreadedChannel& operator=(BasicThreadedChannel&&) = delete;

    void Start();
    void Join();
    void Stop();
protected:
    virtual void Run() = 0;
    virtual void BeforeRun() {}
private:
    void RunnerThread();    
};