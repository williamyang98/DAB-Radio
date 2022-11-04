#include "basic_radio.h"

#include "dab/database/dab_database.h"

#include "easylogging++.h"
#include "fmt/core.h"

#define LOG_MESSAGE(...) CLOG(INFO, "basic-radio") << fmt::format(##__VA_ARGS__)
#define LOG_ERROR(...) CLOG(ERROR, "basic-radio") << fmt::format(##__VA_ARGS__)

BasicRadio::BasicRadio(const DAB_Parameters _params, Basic_Radio_Dependencies* _dependencies)
: params(_params), dependencies(_dependencies)
{
    fic_runner = new BasicFICRunner(params);
    db_manager = new Basic_Database_Manager();
}

BasicRadio::~BasicRadio() {
    channels.clear();
    delete fic_runner;
    delete db_manager;
}

void BasicRadio::Process(viterbi_bit_t* const buf, const int N) {
    if (N != params.nb_frame_bits) {
        LOG_ERROR("Got incorrect number of frame bits {}/{}", N, params.nb_frame_bits);
        return;
    }

    auto* fic_buf = &buf[0];
    auto* msc_buf = &buf[params.nb_fic_bits];

    fic_runner->SetBuffer(fic_buf, params.nb_fic_bits);
    for (auto& [_, channel]: channels) {
        channel->SetBuffer(msc_buf, params.nb_msc_bits);
    }

    // Launch all channel threads
    fic_runner->Start();
    for (auto& [_, channel]: channels) {
        channel->Start();
    }

    // Join them all now
    fic_runner->Join();
    for (auto& [_, channel]: channels) {
        channel->Join();
    }

    UpdateDatabase();
}

BasicAudioChannel* BasicRadio::GetAudioChannel(const subchannel_id_t id) {
    auto lock = std::scoped_lock(mutex_channels);
    auto res = channels.find(id);
    if (res == channels.end()) {
        return NULL; 
    }
    return res->second.get();
}

void BasicRadio::UpdateDatabase() {
    auto& misc_info = *(fic_runner->GetMiscInfo());
    auto* live_db = fic_runner->GetLiveDatabase();
    auto* db_updater = fic_runner->GetDatabaseUpdater();

    db_manager->OnMiscInfo(misc_info);
    const bool is_updated = db_manager->OnDatabaseUpdater(live_db, db_updater);
    if (!is_updated) {
        return;
    }

    auto* db = db_manager->GetDatabase();
    for (auto& subchannel: db->subchannels) {
        AddSubchannel(subchannel.id);
    }
}

bool BasicRadio::AddSubchannel(const subchannel_id_t id) {
    auto res = channels.find(id);
    if (res != channels.end()) {
        return false;
    }

    auto* db = db_manager->GetDatabase();
    auto* subchannel = db->GetSubchannel(id);
    if (subchannel == NULL) {
        LOG_ERROR("Selected subchannel {} which doesn't exist in db", id);
        return false;
    }

    auto* service_component = db->GetServiceComponent_Subchannel(id);
    if (service_component == NULL) {
        LOG_ERROR("Selected subchannel {} has no service component", id);
        return false;
    }

    const auto mode = service_component->transport_mode;
    if (mode != TransportMode::STREAM_MODE_AUDIO) {
        LOG_ERROR("Selected subchannel {} which isn't an audio stream", id);
        return false;
    }

    const auto ascty = service_component->audio_service_type;
    if (ascty != AudioServiceType::DAB_PLUS) {
        LOG_ERROR("Selected subchannel {} isn't a DAB+ stream", id);
        return false;
    }

    // create our instance
    LOG_MESSAGE("Added subchannel {}", id);
    auto lock = std::scoped_lock(mutex_channels);
    res = channels.insert({id, std::make_unique<BasicAudioChannel>(params, *subchannel, dependencies)}).first;
    return true;
}