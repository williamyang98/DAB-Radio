#pragma once

#include <unordered_map>
#include <memory>
#include <mutex>

#include "basic_fic_runner.h"
#include "basic_audio_channel.h"
#include "basic_radio_dependencies.h"
#include "basic_database_manager.h"

#include "dab/constants/dab_parameters.h"

#include "../observable.h"

// Our basic radio
class BasicRadio
{
private:
    const DAB_Parameters params;
    Basic_Radio_Dependencies* dependencies;
    Basic_Database_Manager* db_manager;

    // channels
    BasicFICRunner* fic_runner;
    std::unordered_map<subchannel_id_t, std::unique_ptr<BasicAudioChannel>> channels;
    std::mutex mutex_channels;

    // callbacks
    Observable<subchannel_id_t, BasicAudioChannel*> obs_new_audio_channel;
public:
    BasicRadio(const DAB_Parameters _params, Basic_Radio_Dependencies* _dependencies);
    ~BasicRadio();
    void Process(viterbi_bit_t* const buf, const int N);
    auto* GetDatabaseManager(void) { return db_manager; }
    BasicAudioChannel* GetAudioChannel(const subchannel_id_t id);
    auto& OnNewAudioChannel(void) { return obs_new_audio_channel; }
private:
    void UpdateDatabase();
    bool AddSubchannel(const subchannel_id_t id);
};