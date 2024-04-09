#pragma once

#include <stdint.h>
#include <memory>
#include "dab/constants/dab_parameters.h"
#include "dab/database/dab_database_entities.h"
#include "utility/observable.h"
#include "utility/span.h"
#include "viterbi_config.h"
#include "./basic_msc_runner.h"

class MSC_Decoder;
class MSC_Data_Packet_Processor;
class MSC_Reed_Solomon_Data_Packet_Processor;
class Basic_Slideshow_Manager;
struct MOT_Entity;

class Basic_Data_Packet_Channel: public Basic_MSC_Runner
{
private:
    const DAB_Parameters m_params;
    const Subchannel m_subchannel;
    const DataServiceType m_type;
    std::unique_ptr<MSC_Decoder> m_msc_decoder;
    std::unique_ptr<MSC_Data_Packet_Processor> m_msc_data_packet_processor;
    std::unique_ptr<MSC_Reed_Solomon_Data_Packet_Processor> m_msc_rs_data_packet_processor;
    std::unique_ptr<Basic_Slideshow_Manager> m_slideshow_manager;
    Observable<MOT_Entity> m_obs_MOT_entity;
public:
    explicit Basic_Data_Packet_Channel(const DAB_Parameters& params, Subchannel subchannel, DataServiceType type);
    ~Basic_Data_Packet_Channel() override;
    void Process(tcb::span<const viterbi_bit_t> msc_bits_buf) override;
    auto& GetSlideshowManager() { return *m_slideshow_manager; }
    auto& OnMOTEntity() { return m_obs_MOT_entity; }
private:
    void ProcessNonFECPackets(tcb::span<const uint8_t> buf);
    void ProcessFECPackets(tcb::span<const uint8_t> buf);
};
