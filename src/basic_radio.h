#pragma once

#include <vector>
#include <unordered_map>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "dab/database/dab_database_types.h"
#include "dab/fic/fic_decoder.h"
#include "dab/fic/fig_processor.h"
#include "dab/radio_fig_handler.h"
#include "dab/database/dab_database.h"
#include "dab/database/dab_database_updater.h"
#include "dab/msc/msc_decoder.h"
#include "dab/audio/aac_frame_processor.h"
#include "dab/dab_misc_info.h"
#include "dab/constants/dab_parameters.h"

#include "audio/pcm_player.h"

// Sample implementation classes for how we put all the DAB components together into a radio
class BasicThreadedChannel 
{
private:
    bool is_running;
    bool is_terminated;
    bool is_start;
    bool is_join;
    std::thread* runner_thread;
    std::mutex mutex_start;
    std::condition_variable cv_start;
    std::mutex mutex_join;
    std::condition_variable cv_join;
    std::mutex mutex_terminate;
    std::condition_variable cv_terminate;
    viterbi_bit_t* buf;
    int nb_bytes;
public:
    BasicThreadedChannel();
    virtual ~BasicThreadedChannel();
    void SetBuffer(viterbi_bit_t* const _buf, const int N);
    inline viterbi_bit_t* GetBuffer() { return buf; }
    inline int GetBufferLength() { return nb_bytes; }
    void Start();
    void Join();
    void Stop();
protected:
    virtual void Run() = 0;
private:
    void RunnerThread();    
};

class BasicAudioChannel: public BasicThreadedChannel
{
public:
    bool is_selected = false;
private:
    const DAB_Parameters params;
    const Subchannel subchannel;
    MSC_Decoder* msc_decoder;
    AAC_Frame_Processor* aac_frame_processor;
    PCM_Player* pcm_player;
public:
    BasicAudioChannel(const DAB_Parameters _params, const Subchannel _subchannel);
    ~BasicAudioChannel();
protected:
    virtual void Run();
};

class BasicFICRunner: public BasicThreadedChannel
{
private:
    const DAB_Parameters params;
    DAB_Misc_Info* misc_info;
    DAB_Database* dab_db;
    DAB_Database_Updater* dab_db_updater;
    FIC_Decoder* fic_decoder;
    FIG_Processor* fig_processor;
    Radio_FIG_Handler* fig_handler;
public:
    BasicFICRunner(const DAB_Parameters _params);
    ~BasicFICRunner();
    auto GetLiveDatabase(void) { return dab_db; }
    auto GetDatabaseUpdater(void) { return dab_db_updater; }
    auto GetMiscInfo(void) { return misc_info; }
protected:
    virtual void Run();
};

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