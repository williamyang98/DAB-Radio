#pragma once

#include <unordered_map>
#include <memory>
#include <mutex>

#include "./basic_thread_pool.h"
#include "./basic_fic_runner.h"
#include "./basic_dab_plus_channel.h"
#include "./basic_database_manager.h"
#include "dab/constants/dab_parameters.h"
#include "utility/observable.h"
#include "utility/span.h"

// Our basic radio
class BasicRadio
{
private:
    const DAB_Parameters params;
    BasicThreadPool thread_pool;
    Basic_Database_Manager db_manager;
    BasicFICRunner fic_runner;
    std::unordered_map<subchannel_id_t, std::unique_ptr<Basic_DAB_Plus_Channel>> dab_plus_channels;
    std::mutex mutex_channels;
    Observable<subchannel_id_t, Basic_DAB_Plus_Channel&> obs_dab_plus_channel;
public:
    explicit BasicRadio(const DAB_Parameters& _params, const size_t nb_threads=0);
    ~BasicRadio();
    void Process(tcb::span<const viterbi_bit_t> buf);
    auto& GetDatabaseManager(void) { return db_manager; }
    Basic_DAB_Plus_Channel* Get_DAB_Plus_Channel(const subchannel_id_t id);
    auto& On_DAB_Plus_Channel(void) { return obs_dab_plus_channel; }
    size_t GetTotalThreads() const { return thread_pool.GetTotalThreads(); }
private:
    void UpdateDatabase();
    bool AddSubchannel(const subchannel_id_t id);
};