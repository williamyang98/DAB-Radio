#include "basic_radio.h"

#include "dab/database/dab_database.h"

#include "easylogging++.h"
#include "fmt/core.h"

#define LOG_MESSAGE(...) CLOG(INFO, "basic-radio") << fmt::format(##__VA_ARGS__)
#define LOG_ERROR(...) CLOG(ERROR, "basic-radio") << fmt::format(##__VA_ARGS__)

BasicRadio::BasicRadio(const DAB_Parameters _params)
: params(_params) 
{
    fic_runner = new BasicFICRunner(params);
    valid_dab_db = new DAB_Database();
}

BasicRadio::~BasicRadio() {
    channels.clear();
    delete fic_runner;
    delete valid_dab_db;
}

void BasicRadio::Process(viterbi_bit_t* const buf, const int N) {
    if (N != params.nb_frame_bits) {
        LOG_ERROR("Got incorrect number of frame bits {}/{}", N, params.nb_frame_bits);
        return;
    }

    auto* fic_buf = &buf[0];
    auto* msc_buf = &buf[params.nb_fic_bits];
    {
        auto lock = std::scoped_lock(mutex_channels);
        selected_channels_temp.clear();
        for (auto& [_, channel]: channels) {
            if (channel->is_selected) {
                selected_channels_temp.push_back(channel.get());
            }
        }
    }

    {
        fic_runner->SetBuffer(fic_buf, params.nb_fic_bits);
        for (auto& channel: selected_channels_temp) {
            channel->SetBuffer(msc_buf, params.nb_msc_bits);
        }

        // Launch all channel threads
        fic_runner->Start();
        for (auto& channel: selected_channels_temp) {
            channel->Start();
        }

        // Join them all now
        fic_runner->Join();
        for (auto& channel: selected_channels_temp) {
            channel->Join();
        }
    }

    UpdateDatabase();
}

void BasicRadio::UpdateDatabase() {
    misc_info = *(fic_runner->GetMiscInfo());
    auto* live_db = fic_runner->GetLiveDatabase();
    auto* db_updater = fic_runner->GetDatabaseUpdater();
    
    auto curr_stats = db_updater->GetStatistics();
    const bool is_changed = (previous_stats != curr_stats);
    previous_stats = curr_stats;

    // If there is a change, wait for changes to stabilise
    if (is_changed) {
        is_awaiting_db_update = true;
        nb_cooldown = 0;
        return;
    }

    // If we know the databases are desynced update cooldown
    if (is_awaiting_db_update) {
        nb_cooldown++;
        LOG_MESSAGE("cooldown={}/{}", nb_cooldown, nb_cooldown_max);
    }

    if (nb_cooldown != nb_cooldown_max) {
        return;
    }

    is_awaiting_db_update = false;
    nb_cooldown = 0;
    LOG_MESSAGE("Updating internal database");

    // If the cooldown has been reached, then we consider
    // the databases to be sufficiently stable to copy
    // This is an expensive operation so we should only do it when there are few changes
    {
        auto lock = std::scoped_lock(mutex_db);
        db_updater->ExtractCompletedDatabase(*valid_dab_db);
    }

    // TODO: Auto run subchannels if we are in data scrapeing mode
    // for (auto& subchannel: valid_dab_db->subchannels) {
    //     const auto id = subchannel.id;
    //     if (IsSubchannelAdded(id)) continue;
    //     AddSubchannel(id);
    // }
}

void BasicRadio::AddSubchannel(const subchannel_id_t id) {
    // NOTE: We expect the caller to have this mutex held
    // auto lock = std::scoped_lock(mutex_channels);
    auto res = channels.find(id);
    if (res != channels.end()) {
        // LOG_ERROR("Selected subchannel {} already has an instance running", id);
        auto& v = res->second->is_selected;
        v = !v;
        return;
    }

    auto* db = valid_dab_db;
    auto* subchannel = db->GetSubchannel(id);
    if (subchannel == NULL) {
        LOG_ERROR("Selected subchannel {} which doesn't exist in db", id);
        return;
    }

    auto* service_component = db->GetServiceComponent_Subchannel(id);
    if (service_component == NULL) {
        LOG_ERROR("Selected subchannel {} has no service component", id);
        return;
    }

    const auto mode = service_component->transport_mode;
    if (mode != TransportMode::STREAM_MODE_AUDIO) {
        LOG_ERROR("Selected subchannel {} which isn't an audio stream", id);
        return;
    }

    const auto ascty = service_component->audio_service_type;
    if (ascty != AudioServiceType::DAB_PLUS) {
        LOG_ERROR("Selected subchannel {} isn't a DAB+ stream", id);
        return;
    }

    // create our instance
    LOG_MESSAGE("Added subchannel {}", id);
    res = channels.insert({id, std::make_unique<BasicAudioChannel>(params, *subchannel)}).first;
    res->second->is_selected = true;
}

bool BasicRadio::IsSubchannelAdded(const subchannel_id_t id) {
    // NOTE: This would be extremely slow with alot of subchannels
    // auto lock = std::scoped_lock(mutex_channels);
    auto res = channels.find(id);
    if (res == channels.end()) {
        return false;
    }
    return res->second->is_selected;
}
