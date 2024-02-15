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
    volatile bool m_is_running;
    size_t m_nb_threads;
    std::vector<std::thread> m_task_threads;
    // tasks
    using Task = std::function<void()>;
    int m_total_tasks;
    std::mutex m_mutex_total_tasks;
    std::condition_variable m_cv_wait_task;
    std::queue<Task> m_task_queue;
    // wait all tasks
    bool m_is_wait_all;
    std::condition_variable m_cv_wait_done;
public:
    explicit BasicThreadPool(size_t nb_threads=0) {
        m_total_tasks = 0;
        m_is_running = true;
        m_is_wait_all = false;
        m_nb_threads = nb_threads ? nb_threads : std::thread::hardware_concurrency();

        m_task_threads.reserve(m_nb_threads);
        for (size_t i = 0; i < m_nb_threads; i++) {
            m_task_threads.emplace_back(&BasicThreadPool::RunnerThread, this);
        }
    }
    ~BasicThreadPool() {
        StopAll();
    }
    size_t GetTotalThreads() const { return m_nb_threads; }
    void StopAll() {
        if (!m_is_running) {
            return;
        }

        m_is_running = false;
        m_cv_wait_task.notify_all();
        for (auto& thread: m_task_threads) {
            thread.join();
        }
    }
    void PushTask(const Task& task) {
        auto lock = std::scoped_lock(m_mutex_total_tasks);
        m_task_queue.push(task);
        m_total_tasks++;
        m_cv_wait_task.notify_one();
    }
    void WaitAll() {
        m_is_wait_all = true;
        auto lock = std::unique_lock(m_mutex_total_tasks);
        if (m_total_tasks != 0) {
            m_cv_wait_done.wait(lock, [this] {
                return m_total_tasks == 0;
            });
        }
        m_is_wait_all = false;
    }
private:
    // thread waits for new tasks and runs them
    void RunnerThread() {
        while (m_is_running) {
            auto lock = std::unique_lock(m_mutex_total_tasks);
            m_cv_wait_task.wait(lock, [this] {
                return !m_task_queue.empty() || !m_is_running;
            });

            if (!m_is_running) {
                break;
            }

            auto task = m_task_queue.front();
            m_task_queue.pop();

            lock.unlock();
            task();

            lock.lock();
            m_total_tasks--;

            if (m_is_wait_all) {
                m_cv_wait_done.notify_one();
            }
        }
    }
};