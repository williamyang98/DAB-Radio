#pragma once

#include <stdint.h>
#include <stddef.h>
#include <vector>
#include <optional>
#include <memory>
#include "utility/span.h"

class MOT_Processor;

class MSC_Data_Packet_Processor
{
private:
    std::optional<uint16_t> m_last_address = std::nullopt;
    uint8_t m_last_continuity_index = 0;
    size_t m_total_packets = 0;
    std::vector<uint8_t> m_assembly_buffer;
    std::unique_ptr<MOT_Processor> m_mot_processor;
public:
    MSC_Data_Packet_Processor();
    ~MSC_Data_Packet_Processor();
    size_t ReadPacket(tcb::span<const uint8_t> buf);
    MOT_Processor& Get_MOT_Processor() const { return *m_mot_processor; }
private:
    void PushPiece(tcb::span<const uint8_t> piece);
    void ResetAssembler();
    void HandleDataGroup(tcb::span<const uint8_t> data_group);
};

