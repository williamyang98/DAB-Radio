#include "./basic_data_packet_channel.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <memory>
#include <fmt/format.h>
#include "dab/constants/dab_parameters.h"
#include "dab/database/dab_database_entities.h"
#include "dab/mot/MOT_processor.h"
#include "dab/msc/msc_data_packet_processor.h"
#include "dab/msc/msc_decoder.h"
#include "dab/msc/msc_reed_solomon_data_packet_processor.h"
#include "utility/span.h"
#include "viterbi_config.h"
#include "./basic_radio_logging.h"
#include "./basic_slideshow.h"
#define LOG_MESSAGE(...) BASIC_RADIO_LOG_MESSAGE(fmt::format(__VA_ARGS__))
#define LOG_ERROR(...) BASIC_RADIO_LOG_ERROR(fmt::format(__VA_ARGS__))

Basic_Data_Packet_Channel::Basic_Data_Packet_Channel(const DAB_Parameters& params, Subchannel subchannel, DataServiceType type)
: m_params(params), m_subchannel(subchannel), m_type(type)
{
    assert(subchannel.is_complete);
    assert(subchannel.fec_scheme != FEC_Scheme::UNDEFINED);
    m_msc_rs_data_packet_processor = nullptr;
    m_msc_decoder = std::make_unique<MSC_Decoder>(m_subchannel);
    m_msc_data_packet_processor = std::make_unique<MSC_Data_Packet_Processor>();
    m_slideshow_manager = std::make_unique<Basic_Slideshow_Manager>();
    if (m_subchannel.fec_scheme == FEC_Scheme::REED_SOLOMON) {
        m_msc_rs_data_packet_processor = std::make_unique<MSC_Reed_Solomon_Data_Packet_Processor>();
        m_msc_rs_data_packet_processor->SetCallback([this](tcb::span<const uint8_t> buf, bool is_fec) {
            ProcessNonFECPackets(buf);
        });
    }
    m_msc_data_packet_processor->Get_MOT_Processor().OnEntityComplete().Attach([this](MOT_Entity entity) {
        auto slideshow = m_slideshow_manager->Process_MOT_Entity(entity);
        if (slideshow == nullptr) {
            m_obs_MOT_entity.Notify(entity);
        }
    });
 
    // TODO: Right now we just pass everything through the MOT decoder via the data packet processor
    //       How to handle other object types besides MOT
    (void)m_type;
}

Basic_Data_Packet_Channel::~Basic_Data_Packet_Channel() = default;

void Basic_Data_Packet_Channel::Process(tcb::span<const viterbi_bit_t> msc_bits_buf) {
    BASIC_RADIO_SET_THREAD_NAME(fmt::format("MSC-data-packet-subchannel-{}", m_subchannel.id));

    const int nb_msc_bits = (int)msc_bits_buf.size();
    if (nb_msc_bits != m_params.nb_msc_bits) {
        LOG_ERROR("Got incorrect number of MSC bits {}/{}", nb_msc_bits, m_params.nb_msc_bits);
        return;
    }

    for (int i = 0; i < m_params.nb_cifs; i++) {
        const auto cif_buf = msc_bits_buf.subspan(i*m_params.nb_cif_bits, m_params.nb_cif_bits);
        auto buf = m_msc_decoder->DecodeCIF(cif_buf);
        // The MSC decoder can have 0 bytes if the deinterleaver is still collecting frames
        if (buf.empty()) {
            continue;
        }

        if (m_msc_rs_data_packet_processor) {
            ProcessFECPackets(buf);
        } else {
            ProcessNonFECPackets(buf);
        }
    }
}

void Basic_Data_Packet_Channel::ProcessFECPackets(tcb::span<const uint8_t> buf) {
    while (!buf.empty()) {
        const size_t total_read = m_msc_rs_data_packet_processor->ReadPacket(buf);
        assert(total_read <= buf.size());
        buf = buf.subspan(total_read);
    }
}

void Basic_Data_Packet_Channel::ProcessNonFECPackets(tcb::span<const uint8_t> buf) {
    while (!buf.empty()) {
        const size_t total_read = m_msc_data_packet_processor->ReadPacket(buf);
        assert(total_read <= buf.size());
        buf = buf.subspan(total_read);
    }
}
