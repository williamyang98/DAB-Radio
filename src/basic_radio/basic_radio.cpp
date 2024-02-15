#include "./basic_radio.h"
#include "dab/database/dab_database.h"
#include "dab/database/dab_database_updater.h"
#include "dab/dab_misc_info.h"
#include <fmt/core.h>

#include "./basic_radio_logging.h"
#define LOG_MESSAGE(...) BASIC_RADIO_LOG_MESSAGE(fmt::format(__VA_ARGS__))
#define LOG_ERROR(...) BASIC_RADIO_LOG_ERROR(fmt::format(__VA_ARGS__))

BasicRadio::BasicRadio(const DAB_Parameters& _params, const size_t nb_threads)
: params(_params), thread_pool(nb_threads), fic_runner(_params)
{
    dab_misc_info = std::make_unique<DAB_Misc_Info>();
    dab_database = std::make_unique<DAB_Database>();
    dab_database_stats = std::make_unique<DatabaseUpdaterGlobalStatistics>();
}

BasicRadio::~BasicRadio() = default;

void BasicRadio::Process(tcb::span<const viterbi_bit_t> buf) {
    const int N = (int)buf.size();
    if (N != params.nb_frame_bits) {
        LOG_ERROR("Got incorrect number of frame bits {}/{}", N, params.nb_frame_bits);
        return;
    }

    auto fic_buf = buf.subspan(0,                  params.nb_fic_bits);
    auto msc_buf = buf.subspan(params.nb_fic_bits, params.nb_msc_bits);

    thread_pool.PushTask([this, &fic_buf] {
        fic_runner.Process(fic_buf);
    });

    for (auto& [_, channel]: dab_plus_channels) {
        auto& dab_plus_channel = *(channel.get());
        thread_pool.PushTask([&dab_plus_channel, &msc_buf] {
            dab_plus_channel.Process(msc_buf);
        });
    }

    thread_pool.WaitAll();

    UpdateAfterProcessing();
}

Basic_DAB_Plus_Channel* BasicRadio::Get_DAB_Plus_Channel(const subchannel_id_t id) {
    auto res = dab_plus_channels.find(id);
    if (res == dab_plus_channels.end()) {
        return nullptr; 
    }
    return res->second.get();
}

void BasicRadio::UpdateAfterProcessing() {
    auto lock = std::scoped_lock(mutex_data);
    const auto& new_misc_info = fic_runner.GetMiscInfo();
    const auto& dab_database_updater = fic_runner.GetDatabaseUpdater();
    const auto& new_dab_database = dab_database_updater.GetDatabase();
    const auto& new_dab_database_stats = dab_database_updater.GetStatistics();


    *dab_misc_info = new_misc_info;

    const bool is_updated = new_dab_database_stats != *dab_database_stats;
    if (!is_updated) return;
    *dab_database = new_dab_database;
    *dab_database_stats = new_dab_database_stats;

    for (auto& subchannel: dab_database->subchannels) {
        if (dab_plus_channels.find(subchannel.id) != dab_plus_channels.end()) {
            continue;
        }
 
        ServiceComponent* service_component = nullptr;
        for (auto& e: dab_database->service_components) {
            if (e.subchannel_id == subchannel.id) {
                service_component = &e;
                break;
            }
        }
        if (!service_component) {
            continue;
        }

        const auto mode = service_component->transport_mode;
        if (mode != TransportMode::STREAM_MODE_AUDIO) {
            continue;
        }

        const auto ascty = service_component->audio_service_type;
        if (ascty != AudioServiceType::DAB_PLUS) {
            continue;
        }

        LOG_MESSAGE("Added subchannel {}", subchannel.id);
        auto channel_ptr = std::make_unique<Basic_DAB_Plus_Channel>(params, subchannel);
        auto& channel = *(channel_ptr.get());
        dab_plus_channels.insert({subchannel.id, std::move(channel_ptr)});
        obs_dab_plus_channel.Notify(subchannel.id, channel);
    }
}