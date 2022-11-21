#pragma once

#include <stdint.h>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>

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
private:
    const char* label = "";
    uint64_t data = 0;

    // Log all traces with a unique hash
    bool is_trace_logging = false;
    // Log a single snapshot of the unique trace, or continuously update it
    bool is_trace_logging_snapshot = true;

    int stack_index = 0;
    int results_length = 0;
    profile_trace_t results;
    profile_trace_t prev_results;

    // Log all unique stack traces
    // This is useful if the stack trace varies each call and we are interested 
    // in profiling each of these possible variations
    profile_trace_logger_t profiler_logger;

    std::mutex mutex_prev_results;
    std::mutex mutex_profiler_logger;
public:
    InstrumentorThread() {
        results.reserve(200);
        prev_results.reserve(200);
    }

    std::pair<int,int> PushStackIndex() { 
        stack_index++; 
        results_length++;
        results.resize(results_length);
        return {stack_index-1, results_length-1};
    }

    void WriteProfile(ProfileResult&& res, int result_index) {
        results[result_index] = res;
        PopStackIndex();
    }

    auto& GetPrevTrace() { return prev_results; }
    auto& GetPrevTraceMutex() { return mutex_prev_results; }
    auto& GetTraceLogs() { return profiler_logger; }
    auto& GetTraceLogsMutex() { return mutex_profiler_logger; }

    const char* GetLabel() const { return label; }
    void SetLabel(const char* _label) { label = _label; }

    void SetData(uint64_t _data) { data = _data; }
    uint64_t GetData() const { return data; }

    void SetIsLogTraces(bool _is_trace_logging) { is_trace_logging = _is_trace_logging; }
    bool GetIsLogTraces() const { return is_trace_logging; }

    void SetIsLogTracesSnapshot(bool _is_snapshot) { is_trace_logging_snapshot = _is_snapshot; }
    bool GetIsLogTracesSnapshot() const { return is_trace_logging_snapshot; }
private:
    int PopStackIndex() { 
        stack_index--; 
        if (stack_index == 0) {
            UpdateResults();
        }

        return stack_index;
    }
    void UpdateResults() {
        if (is_trace_logging) {
            auto lock = std::scoped_lock(mutex_profiler_logger);
            const auto key = CalculateHash(results);
            auto res = profiler_logger.find(key);
            if (res == profiler_logger.end()) {
                profiler_logger.insert({key, {1, results}});
            } else {
                res->second.count++;
                // We continously update the unique trace
                if (!is_trace_logging_snapshot) {
                    res->second.trace = results;
                }
            }
        }
        {
            auto lock = std::scoped_lock(mutex_prev_results);
            std::swap(results, prev_results);
            results_length = 0;
        }
    }
    uint64_t CalculateHash(profile_trace_t& stack_trace) {
        uint64_t hash = 0;
        hash = (hash >> 32) ^ (hash << 32) ^ stack_trace.size();
        for (auto& e: stack_trace) {
            hash = (hash >> 32) ^ (hash << 32) ^ e.stack_index;
            // if (e.stack_index >= 2) continue;
            if (e.name != NULL) {
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
    std::unordered_map<std::thread::id, InstrumentorThread> threads;
    std::vector<std::pair<std::thread::id, InstrumentorThread&>> threads_ref_list;
    int64_t base_dt;
private:
    Instrumentor()
    {
        threads_ref_list.reserve(100);
        base_dt = ConvertMicros(GetNow());
    }
public:
    InstrumentorThread& GetInstrumentorThread(std::thread::id id) {
        auto res = threads.find(id);
        if (res == threads.end()) {
            // threads.emplace(id);
            res = threads.try_emplace(id).first;
            // res = threads.insert({id, {}}).first;
            threads_ref_list.push_back({id, res->second});
        }
        return res->second;
    }
    InstrumentorThread& GetInstrumentorThread(void) {
        return GetInstrumentorThread(std::this_thread::get_id());
    }
    auto& GetThreadsList() {
        return threads_ref_list;
    }
    const auto& GetBase() {
        return base_dt;
    }
    static Instrumentor& Get()
    {
        static Instrumentor instance;
        return instance;
    }
};

// Scoped timer
class InstrumentationTimer
{
private:
    const char* name;
    bool is_stopped;
    int stack_index;
    int result_index;
    std::chrono::time_point<std::chrono::high_resolution_clock> time_start;
    InstrumentorThread* thread_ptr;
    std::thread::id thread_id;
public:
    InstrumentationTimer(const char* _name)
    : name(_name), is_stopped(false)
    {
        thread_id = std::this_thread::get_id();
        auto& thread = Instrumentor::Get().GetInstrumentorThread(thread_id);
        thread_ptr = &thread;
        auto res = thread_ptr->PushStackIndex();
        stack_index = res.first;
        result_index = res.second;
        time_start = GetNow();
    }

    ~InstrumentationTimer() {
        if (!is_stopped) {
            Stop();
        }
    }

    void Stop() {
        is_stopped = true;
        auto time_end = GetNow();
        auto dt_start = ConvertMicros(time_start) - Instrumentor::Get().GetBase();
        auto dt_end = ConvertMicros(time_end) - Instrumentor::Get().GetBase();
        thread_ptr->WriteProfile({ name, stack_index, dt_start, dt_end }, result_index);
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
#define PROFILE_BEGIN_FUNC() auto timer_##__PRETTY_FUNCTION__ = InstrumentationTimer(__PRETTY_FUNCTION__)
#define PROFILE_BEGIN(label) auto timer_##label = InstrumentationTimer(#label)
#define PROFILE_END(label) timer_##label.Stop()
#define PROFILE_TAG_THREAD(label) Instrumentor::Get().GetInstrumentorThread().SetLabel(label)
#define PROFILE_TAG_DATA_THREAD(data) Instrumentor::Get().GetInstrumentorThread().SetData(data)
#define PROFILE_ENABLE_TRACE_LOGGING(is_log) Instrumentor::Get().GetInstrumentorThread().SetIsLogTraces(is_log) 
#define PROFILE_ENABLE_TRACE_LOGGING_CONTINUOUS(is_continuous) Instrumentor::Get().GetInstrumentorThread().SetIsLogTracesSnapshot(!is_continuous)
#endif