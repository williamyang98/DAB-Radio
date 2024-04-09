#pragma once

#include <stddef.h>
#include <stdint.h>
#include <chrono>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

// Crossplatform pretty function
#ifdef _MSC_VER
#define __PRETTY_FUNCTION__ __FUNCSIG__
#endif

// Get time stamps
static std::chrono::time_point<std::chrono::high_resolution_clock> GetNow() {
    return std::chrono::high_resolution_clock::now();
}

static int64_t ConvertMillis(const std::chrono::time_point<std::chrono::high_resolution_clock>& time) {
    return std::chrono::time_point_cast<std::chrono::milliseconds>(time).time_since_epoch().count();
}

static int64_t ConvertMicros(const std::chrono::time_point<std::chrono::high_resolution_clock>& time) {
    return std::chrono::time_point_cast<std::chrono::microseconds>(time).time_since_epoch().count();
}

static int64_t ConvertNanos(const std::chrono::time_point<std::chrono::high_resolution_clock>& time) {
    return std::chrono::time_point_cast<std::chrono::nanoseconds>(time).time_since_epoch().count();
}

struct ProfileResult
{
    const char* name;
    int stack_index;
    int64_t start, end;
};

// Store stack trace for each thread
class InstrumentorThread 
{
public:
    typedef std::vector<ProfileResult> profile_trace_t;
    struct TraceLog {
        int count = 0;
        profile_trace_t trace;
    };
    typedef std::unordered_map<uint64_t, TraceLog> profile_trace_logger_t;
    struct Descriptor {
        size_t symbol_start = 0;
        size_t symbol_end = 0;
    };
private:
    const char* m_label = "";
    std::optional<Descriptor> m_data = std::nullopt;

    // Log all traces with a unique hash
    bool m_is_trace_logging = false;
    // Log a single snapshot of the unique trace, or continuously update it
    bool m_is_trace_logging_snapshot = true;

    int m_stack_index = 0;
    int m_results_length = 0;
    profile_trace_t m_results;
    profile_trace_t m_prev_results;

    // Log all unique stack traces
    // This is useful if the stack trace varies each call and we are interested 
    // in profiling each of these possible variations
    profile_trace_logger_t m_profiler_logger;

    std::mutex m_mutex_prev_results;
    std::mutex m_mutex_profiler_logger;
public:
    InstrumentorThread() {
        m_results.reserve(200);
        m_prev_results.reserve(200);
    }

    std::pair<int,int> PushStackIndex() { 
        m_stack_index++; 
        m_results_length++;
        m_results.resize(m_results_length);
        return {m_stack_index-1, m_results_length-1};
    }

    void WriteProfile(ProfileResult&& res, int result_index) {
        m_results[result_index] = res;
        PopStackIndex();
    }

    auto& GetPrevTrace() { return m_prev_results; }
    auto& GetPrevTraceMutex() { return m_mutex_prev_results; }
    auto& GetTraceLogs() { return m_profiler_logger; }
    auto& GetTraceLogsMutex() { return m_mutex_profiler_logger; }

    const char* GetLabel() const { return m_label; }
    void SetLabel(const char* _label) { m_label = _label; }

    void SetData(const std::optional<Descriptor>& data) { m_data = data; }
    const auto& GetData() const { return m_data; }

    void SetIsLogTraces(bool _is_trace_logging) { m_is_trace_logging = _is_trace_logging; }
    bool GetIsLogTraces() const { return m_is_trace_logging; }

    void SetIsLogTracesSnapshot(bool _is_snapshot) { m_is_trace_logging_snapshot = _is_snapshot; }
    bool GetIsLogTracesSnapshot() const { return m_is_trace_logging_snapshot; }
private:
    int PopStackIndex() { 
        m_stack_index--; 
        if (m_stack_index == 0) {
            UpdateResults();
        }

        return m_stack_index;
    }
    void UpdateResults() {
        if (m_is_trace_logging) {
            auto lock = std::scoped_lock(m_mutex_profiler_logger);
            const auto key = CalculateHash(m_results);
            auto res = m_profiler_logger.find(key);
            if (res == m_profiler_logger.end()) {
                m_profiler_logger.insert({key, {1, m_results}});
            } else {
                res->second.count++;
                // We continously update the unique trace
                if (!m_is_trace_logging_snapshot) {
                    res->second.trace = m_results;
                }
            }
        }
        {
            auto lock = std::scoped_lock(m_mutex_prev_results);
            std::swap(m_results, m_prev_results);
            m_results_length = 0;
        }
    }
    uint64_t CalculateHash(const profile_trace_t& stack_trace) const {
        uint64_t hash = 0;
        hash = (hash >> 32) ^ (hash << 32) ^ stack_trace.size();
        for (const auto& e: stack_trace) {
            hash = (hash >> 32) ^ (hash << 32) ^ e.stack_index;
            // if (e.stack_index >= 2) continue;
            if (e.name != nullptr) {
                for (const char* c = e.name; (*c) != 0; c++) {
                    hash = (hash >> 8) ^ (hash << 8) ^ (uint64_t)(*c);
                }
            }
        }
        return hash;
    }
};

// Store instrumentation for each thread
class Instrumentor
{
private:
    std::unordered_map<std::thread::id, InstrumentorThread> m_threads;
    std::vector<std::pair<std::thread::id, InstrumentorThread&>> m_threads_ref_list;
    std::mutex m_mutex_threads_list;
    int64_t m_base_dt;
private:
    Instrumentor()
    {
        m_threads_ref_list.reserve(100);
        m_base_dt = ConvertMicros(GetNow());
    }
public:
    InstrumentorThread& GetInstrumentorThread(std::thread::id id) {
        auto lock = std::unique_lock(m_mutex_threads_list);
        auto res = m_threads.find(id);
        if (res == m_threads.end()) {
            res = m_threads.try_emplace(id).first;
            m_threads_ref_list.push_back({id, res->second});
        }
        return res->second;
    }
    InstrumentorThread& GetInstrumentorThread(void) {
        return GetInstrumentorThread(std::this_thread::get_id());
    }
    auto& GetMutexThreadsList() { return m_mutex_threads_list; }
    auto& GetThreadsList() { return m_threads_ref_list; }
    const auto& GetBase() { return m_base_dt; }
    static Instrumentor& Get() {
        static Instrumentor instance;
        return instance;
    }
};

// Scoped timer
class InstrumentationTimer
{
private:
    const char* m_name;
    bool m_is_stopped;
    int m_stack_index;
    int m_result_index;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_time_start;
    InstrumentorThread* m_thread_ptr;
    std::thread::id m_thread_id;
public:
    explicit InstrumentationTimer(const char* name)
    : m_name(name), m_is_stopped(false)
    {
        m_thread_id = std::this_thread::get_id();
        auto& thread = Instrumentor::Get().GetInstrumentorThread(m_thread_id);
        m_thread_ptr = &thread;
        auto res = m_thread_ptr->PushStackIndex();
        m_stack_index = res.first;
        m_result_index = res.second;
        m_time_start = GetNow();
    }

    ~InstrumentationTimer() {
        if (!m_is_stopped) {
            Stop();
        }
    }

    void Stop() {
        m_is_stopped = true;
        auto time_end = GetNow();
        auto dt_start = ConvertMicros(m_time_start) - Instrumentor::Get().GetBase();
        auto dt_end = ConvertMicros(time_end) - Instrumentor::Get().GetBase();
        m_thread_ptr->WriteProfile({ m_name, m_stack_index, dt_start, dt_end }, m_result_index);
    }
};

#if !PROFILE_ENABLE
#define PROFILE_BEGIN_FUNC() (void)0
#define PROFILE_BEGIN(label) (void)0
#define PROFILE_END(label) (void)0
#define PROFILE_TAG_THREAD(label) (void)0
#define PROFILE_TAG_DATA_THREAD(data) (void)0
#define PROFILE_ENABLE_TRACE_LOGGING(is_log) (void)0
#define PROFILE_ENABLE_TRACE_LOGGING_CONTINUOUS(is_continuous) (void)0
#else
#define PROFILE_BEGIN_FUNC() auto timer_func = InstrumentationTimer(__PRETTY_FUNCTION__)
#define PROFILE_BEGIN(label) auto timer_##label = InstrumentationTimer(#label)
#define PROFILE_END(label) timer_##label.Stop()
#define PROFILE_TAG_THREAD(label) Instrumentor::Get().GetInstrumentorThread().SetLabel(label)
#define PROFILE_TAG_DATA_THREAD(data) Instrumentor::Get().GetInstrumentorThread().SetData(data)
#define PROFILE_ENABLE_TRACE_LOGGING(is_log) Instrumentor::Get().GetInstrumentorThread().SetIsLogTraces(is_log) 
#define PROFILE_ENABLE_TRACE_LOGGING_CONTINUOUS(is_continuous) Instrumentor::Get().GetInstrumentorThread().SetIsLogTracesSnapshot(!is_continuous)
#endif