#include "./basic_radio.h"
#include <stddef.h>
#include <memory>
#include <mutex>
#include <fmt/format.h>
#include "dab/constants/dab_parameters.h"
#include "dab/dab_misc_info.h"
#include "dab/database/dab_database.h"
#include "dab/database/dab_database_entities.h"
#include "dab/database/dab_database_types.h"
#include "dab/database/dab_database_updater.h"
#include "utility/span.h"
#include "viterbi_config.h"
#include "./basic_audio_channel.h"
#include "./basic_dab_channel.h"
#include "./basic_dab_plus_channel.h"
#include "./basic_data_packet_channel.h"
#include "./basic_fic_runner.h"
#include "./basic_msc_runner.h"
#include "./basic_radio_logging.h"
#include "./basic_thread_pool.h"
#define LOG_MESSAGE(...) BASIC_RADIO_LOG_MESSAGE(fmt::format(__VA_ARGS__))
#define LOG_ERROR(...) BASIC_RADIO_LOG_ERROR(fmt::format(__VA_ARGS__))

BasicRadio::BasicRadio(const DAB_Parameters& params, const size_t nb_threads)
: m_params(params)
{
    m_thread_pool = std::make_unique<BasicThreadPool>(nb_threads);
    m_fic_runner = std::make_unique<BasicFICRunner>(m_params);
    m_dab_misc_info = std::make_unique<DAB_Misc_Info>();
    m_dab_database = std::make_unique<DAB_Database>();
    m_dab_database_stats = std::make_unique<DatabaseUpdaterGlobalStatistics>();
}

BasicRadio::~BasicRadio() = default;

size_t BasicRadio::GetTotalThreads() const {
    return m_thread_pool->GetTotalThreads();
}

void BasicRadio::Process(tcb::span<const viterbi_bit_t> buf) {
    const int N = (int)buf.size();
    if (N != m_params.nb_frame_bits) {
        LOG_ERROR("Got incorrect number of frame bits {}/{}", N, m_params.nb_frame_bits);
        return;
    }

    auto fic_buf = buf.subspan(0, m_params.nb_fic_bits);
    auto msc_buf = buf.subspan(m_params.nb_fic_bits, m_params.nb_msc_bits);

    m_thread_pool->PushTask([this, fic_buf] {
        m_fic_runner->Process(fic_buf);
    });

    for (const auto& [_, msc_runner]: m_msc_runners) {
        const auto runner = msc_runner;
        m_thread_pool->PushTask([runner, msc_buf]() {
            runner->Process(msc_buf);
        });
    }

    m_thread_pool->WaitAll();

    UpdateAfterProcessing();
}

Basic_Audio_Channel* BasicRadio::Get_Audio_Channel(const subchannel_id_t id) {
    auto res = m_audio_channels.find(id);
    if (res == m_audio_channels.end()) {
        return nullptr; 
    }
    return res->second.get();
}

Basic_Data_Packet_Channel* BasicRadio::Get_Data_Packet_Channel(const subchannel_id_t id) {
    auto res = m_data_packet_channels.find(id);
    if (res == m_data_packet_channels.end()) {
        return nullptr; 
    }
    return res->second.get();
}

void BasicRadio::UpdateAfterProcessing() {
    auto lock = std::scoped_lock(m_mutex_data);
    const auto& new_misc_info = m_fic_runner->GetMiscInfo();
    const auto& dab_database_updater = m_fic_runner->GetDatabaseUpdater();
    const auto& new_dab_database = dab_database_updater.GetDatabase();
    const auto& new_dab_database_stats = dab_database_updater.GetStatistics();


    *m_dab_misc_info = new_misc_info;

    const bool is_updated = new_dab_database_stats != *m_dab_database_stats;
    if (!is_updated) return;
    *m_dab_database = new_dab_database;
    *m_dab_database_stats = new_dab_database_stats;

    for (auto& subchannel: m_dab_database->subchannels) {
        if (!subchannel.is_complete) continue;

        if (m_msc_runners.find(subchannel.id) != m_msc_runners.end()) {
            continue;
        }
 
        const ServiceComponent* service_component = nullptr;
        for (auto& e: m_dab_database->service_components) {
            if (e.subchannel_id == subchannel.id) {
                service_component = &e;
                break;
            }
        }
        if (!service_component) {
            continue;
        }
        if (!service_component->is_complete) {
            continue;
        }

        const auto mode = service_component->transport_mode;
        const auto audio_type = service_component->audio_service_type;
        const auto data_type = service_component->data_service_type;

        if (audio_type == AudioServiceType::DAB_PLUS && mode == TransportMode::STREAM_MODE_AUDIO) {
            LOG_MESSAGE("Added DAB+ subchannel {}", subchannel.id);
            auto channel = std::make_shared<Basic_DAB_Plus_Channel>(m_params, subchannel, audio_type);
            m_msc_runners.insert({ subchannel.id, channel });
            m_audio_channels.insert({ subchannel.id, channel });
            m_obs_audio_channel.Notify(subchannel.id, *channel);
            continue;
        }

        if (audio_type == AudioServiceType::DAB && mode == TransportMode::STREAM_MODE_AUDIO) {
            LOG_MESSAGE("Added DAB subchannel {}", subchannel.id);
            auto channel = std::make_shared<Basic_DAB_Channel>(m_params, subchannel, audio_type);
            m_msc_runners.insert({ subchannel.id, channel });
            m_audio_channels.insert({ subchannel.id, channel });
            m_obs_audio_channel.Notify(subchannel.id, *channel);
            continue;
        } 
 
        // DOC: EN 300 401
        // Clause: 5.3.5 FEC for MSC packet mode
        // Data packet channels require the FEC scheme to be defined for outer encoding
        if (mode == TransportMode::PACKET_MODE_DATA && (subchannel.fec_scheme != FEC_Scheme::UNDEFINED)) {
            LOG_MESSAGE("Added data packet subchannel {}", subchannel.id);
            auto channel = std::make_shared<Basic_Data_Packet_Channel>(m_params, subchannel, data_type);
            m_msc_runners.insert({ subchannel.id, channel });
            m_data_packet_channels.insert({ subchannel.id, channel });
            m_obs_data_packet_channel.Notify(subchannel.id, *channel);
            continue;
        }
    }
}