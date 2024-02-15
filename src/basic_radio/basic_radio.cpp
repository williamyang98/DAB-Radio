#include "./basic_radio.h"
#include "dab/database/dab_database.h"
#include "dab/database/dab_database_updater.h"
#include "dab/dab_misc_info.h"
#include <fmt/core.h>

#include "./basic_radio_logging.h"
#define LOG_MESSAGE(...) BASIC_RADIO_LOG_MESSAGE(fmt::format(__VA_ARGS__))
#define LOG_ERROR(...) BASIC_RADIO_LOG_ERROR(fmt::format(__VA_ARGS__))

BasicRadio::BasicRadio(const DAB_Parameters& _params, const size_t nb_threads)
: m_params(_params), m_thread_pool(nb_threads), m_fic_runner(_params)
{
    m_dab_misc_info = std::make_unique<DAB_Misc_Info>();
    m_dab_database = std::make_unique<DAB_Database>();
    m_dab_database_stats = std::make_unique<DatabaseUpdaterGlobalStatistics>();
}

BasicRadio::~BasicRadio() = default;

void BasicRadio::Process(tcb::span<const viterbi_bit_t> buf) {
    const int N = (int)buf.size();
    if (N != m_params.nb_frame_bits) {
        LOG_ERROR("Got incorrect number of frame bits {}/{}", N, m_params.nb_frame_bits);
        return;
    }

    auto fic_buf = buf.subspan(0,                  m_params.nb_fic_bits);
    auto msc_buf = buf.subspan(m_params.nb_fic_bits, m_params.nb_msc_bits);

    m_thread_pool.PushTask([this, &fic_buf] {
        m_fic_runner.Process(fic_buf);
    });

    for (auto& [_, channel]: m_dab_plus_channels) {
        auto& dab_plus_channel = *(channel.get());
        m_thread_pool.PushTask([&dab_plus_channel, &msc_buf] {
            dab_plus_channel.Process(msc_buf);
        });
    }

    m_thread_pool.WaitAll();

    UpdateAfterProcessing();
}

Basic_DAB_Plus_Channel* BasicRadio::Get_DAB_Plus_Channel(const subchannel_id_t id) {
    auto res = m_dab_plus_channels.find(id);
    if (res == m_dab_plus_channels.end()) {
        return nullptr; 
    }
    return res->second.get();
}

void BasicRadio::UpdateAfterProcessing() {
    auto lock = std::scoped_lock(m_mutex_data);
    const auto& new_misc_info = m_fic_runner.GetMiscInfo();
    const auto& dab_database_updater = m_fic_runner.GetDatabaseUpdater();
    const auto& new_dab_database = dab_database_updater.GetDatabase();
    const auto& new_dab_database_stats = dab_database_updater.GetStatistics();


    *m_dab_misc_info = new_misc_info;

    const bool is_updated = new_dab_database_stats != *m_dab_database_stats;
    if (!is_updated) return;
    *m_dab_database = new_dab_database;
    *m_dab_database_stats = new_dab_database_stats;

    for (auto& subchannel: m_dab_database->subchannels) {
        if (m_dab_plus_channels.find(subchannel.id) != m_dab_plus_channels.end()) {
            continue;
        }
 
        ServiceComponent* service_component = nullptr;
        for (auto& e: m_dab_database->service_components) {
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
        auto channel_ptr = std::make_unique<Basic_DAB_Plus_Channel>(m_params, subchannel);
        auto& channel = *(channel_ptr.get());
        m_dab_plus_channels.insert({subchannel.id, std::move(channel_ptr)});
        m_obs_dab_plus_channel.Notify(subchannel.id, channel);
    }
}