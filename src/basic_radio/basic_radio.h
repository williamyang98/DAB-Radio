#pragma once

#include <vector>
#include <unordered_map>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "basic_fic_runner.h"
#include "basic_audio_channel.h"

#include "dab/constants/dab_parameters.h"
#include "dab/dab_misc_info.h"
#include "dab/database/dab_database_updater.h"

class DAB_Database;

// Our basic radio
class BasicRadio
{
private:
    const DAB_Parameters params;
    DAB_Misc_Info misc_info;
    // keep track of database with completed entries
    DAB_Database* valid_dab_db;
    DAB_Database_Updater::Statistics previous_stats;
    bool is_awaiting_db_update = false;
    int nb_cooldown = 0;
    const int nb_cooldown_max = 10;
    std::mutex mutex_db;
    std::mutex mutex_channels;
    // channels
    BasicFICRunner* fic_runner;
    std::vector<BasicAudioChannel*> selected_channels_temp;
    std::unordered_map<subchannel_id_t, std::unique_ptr<BasicAudioChannel>> channels;
public:
    BasicRadio(const DAB_Parameters _params);
    ~BasicRadio();
    void Process(viterbi_bit_t* const buf, const int N);
    const auto& GetDABMiscInfo(void) { return misc_info; }
    // NOTE: you must get the mutex associated with this
    auto* GetDatabase(void) { return valid_dab_db; }
    auto& GetDatabaseMutex(void) { return mutex_db; }
    auto GetDatabaseStatistics(void) { return previous_stats; }
    void AddSubchannel(const subchannel_id_t id);
    bool IsSubchannelAdded(const subchannel_id_t id);
    auto& GetChannelsMutex(void) { return mutex_channels; }
private:
    void UpdateDatabase();
};