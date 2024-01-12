#pragma once

#include <unordered_map>
#include <memory>
#include <mutex>

#include "./basic_thread_pool.h"
#include "./basic_fic_runner.h"
#include "./basic_dab_plus_channel.h"
#include "dab/constants/dab_parameters.h"
#include "utility/observable.h"
#include "utility/span.h"

struct DAB_Database;
struct DAB_Misc_Info;
struct DatabaseUpdaterGlobalStatistics;

// Our basic radio
class BasicRadio
{
private:
    const DAB_Parameters params;
    BasicThreadPool thread_pool;
    BasicFICRunner fic_runner;
    std::mutex mutex_data;
    std::unique_ptr<DAB_Misc_Info> dab_misc_info;
    std::unique_ptr<DAB_Database> dab_database;
    std::unique_ptr<DatabaseUpdaterGlobalStatistics> dab_database_stats;
    std::unordered_map<subchannel_id_t, std::unique_ptr<Basic_DAB_Plus_Channel>> dab_plus_channels;
    Observable<subchannel_id_t, Basic_DAB_Plus_Channel&> obs_dab_plus_channel;
public:
    explicit BasicRadio(const DAB_Parameters& _params, const size_t nb_threads=0);
    ~BasicRadio();
    void Process(tcb::span<const viterbi_bit_t> buf);
    Basic_DAB_Plus_Channel* Get_DAB_Plus_Channel(const subchannel_id_t id);
    auto& GetMutex() { return mutex_data; }
    auto& GetMiscInfo() { return *(dab_misc_info.get()); }
    auto& GetDatabase() { return *(dab_database.get()); }
    auto& GetDatabaseStatistics() { return *(dab_database_stats.get()); }
    auto& On_DAB_Plus_Channel() { return obs_dab_plus_channel; }
    size_t GetTotalThreads() const { return thread_pool.GetTotalThreads(); }
private:
    void UpdateAfterProcessing();
};