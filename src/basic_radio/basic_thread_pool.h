#pragma once

#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <queue>
#include <vector>
#include <stddef.h>

// simple thread pool to decode FIC and MSC channels across all cores
class BasicThreadPool 
{
private:
    // threads
    volatile bool is_running;
    size_t nb_threads;
    std::vector<std::thread> task_threads;
    // tasks
    using Task = std::function<void()>;
    int total_tasks;
    std::mutex mutex_total_tasks;
    std::condition_variable cv_wait_task;
    std::queue<Task> task_queue;
    // wait all tasks
    bool is_wait_all;
    std::condition_variable cv_wait_done;
public:
    explicit BasicThreadPool(size_t _nb_threads=0) {
        total_tasks = 0;
        is_running = true;
        is_wait_all = false;
        nb_threads = _nb_threads ? _nb_threads : std::thread::hardware_concurrency();

        task_threads.reserve(nb_threads);
        for (size_t i = 0; i < nb_threads; i++) {
            task_threads.emplace_back(&BasicThreadPool::RunnerThread, this);
        }
    }
    ~BasicThreadPool() {
        StopAll();
    }
    size_t GetTotalThreads() const { return nb_threads; }
    void StopAll() {
        if (!is_running) {
            return;
        }

        is_running = false;
        cv_wait_task.notify_all();
        for (auto& thread: task_threads) {
            thread.join();
        }
    }
    void PushTask(const Task& task) {
        auto lock = std::scoped_lock(mutex_total_tasks);
        task_queue.push(task);
        total_tasks++;
        cv_wait_task.notify_one();
    }
    void WaitAll() {
        is_wait_all = true;
        auto lock = std::unique_lock(mutex_total_tasks);
        if (total_tasks != 0) {
            cv_wait_done.wait(lock, [this] {
                return total_tasks == 0;
            });
        }
        is_wait_all = false;
    }
private:
    // thread waits for new tasks and runs them
    void RunnerThread() {
        while (is_running) {
            auto lock = std::unique_lock(mutex_total_tasks);
            cv_wait_task.wait(lock, [this] {
                return !task_queue.empty() || !is_running;
            });

            if (!is_running) {
                break;
            }

            auto task = task_queue.front();
            task_queue.pop();

            lock.unlock();
            task();

            lock.lock();
            total_tasks--;

            if (is_wait_all) {
                cv_wait_done.notify_one();
            }
        }
    }
};