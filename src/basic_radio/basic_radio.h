#pragma once

#include <stddef.h>
#include <memory>
#include <mutex>
#include <unordered_map>
#include "dab/constants/dab_parameters.h"
#include "dab/database/dab_database_types.h"
#include "utility/observable.h"
#include "utility/span.h"
#include "viterbi_config.h"

struct DAB_Database;
struct DAB_Misc_Info;
struct DatabaseUpdaterGlobalStatistics;
class BasicThreadPool;
class BasicFICRunner;
class Basic_MSC_Runner;
class Basic_Audio_Channel;
class Basic_Data_Packet_Channel;

// Our basic radio
class BasicRadio
{
private:
    const DAB_Parameters m_params;
    std::unique_ptr<BasicThreadPool> m_thread_pool;
    std::unique_ptr<BasicFICRunner> m_fic_runner;
    std::unordered_map<subchannel_id_t, std::shared_ptr<Basic_MSC_Runner>> m_msc_runners;
    std::mutex m_mutex_data;
    std::unique_ptr<DAB_Misc_Info> m_dab_misc_info;
    std::unique_ptr<DAB_Database> m_dab_database;
    std::unique_ptr<DatabaseUpdaterGlobalStatistics> m_dab_database_stats;
    std::unordered_map<subchannel_id_t, std::shared_ptr<Basic_Audio_Channel>> m_audio_channels;
    std::unordered_map<subchannel_id_t, std::shared_ptr<Basic_Data_Packet_Channel>> m_data_packet_channels;
    Observable<subchannel_id_t, Basic_Audio_Channel&> m_obs_audio_channel;
    Observable<subchannel_id_t, Basic_Data_Packet_Channel&> m_obs_data_packet_channel;
public:
    explicit BasicRadio(const DAB_Parameters& params, const size_t nb_threads=0);
    ~BasicRadio();
    void Process(tcb::span<const viterbi_bit_t> buf);
    Basic_Audio_Channel* Get_Audio_Channel(const subchannel_id_t id);
    Basic_Data_Packet_Channel* Get_Data_Packet_Channel(const subchannel_id_t id);
    auto& GetMutex() { return m_mutex_data; }
    auto& GetMiscInfo() { return *(m_dab_misc_info.get()); }
    auto& GetDatabase() { return *(m_dab_database.get()); }
    auto& GetDatabaseStatistics() { return *(m_dab_database_stats.get()); }
    auto& On_Audio_Channel() { return m_obs_audio_channel; }
    auto& On_Data_Packet_Channel() { return m_obs_data_packet_channel; }
    size_t GetTotalThreads() const;
private:
    void UpdateAfterProcessing();
};