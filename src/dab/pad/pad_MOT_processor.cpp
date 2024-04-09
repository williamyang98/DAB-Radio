#include "./pad_MOT_processor.h"
#include <stddef.h>
#include <stdint.h>
#include <memory>
#include <fmt/format.h>
#include "utility/span.h"
#include "../dab_logging.h"
#include "../mot/MOT_processor.h"
#include "../msc/msc_data_group_processor.h"
#define TAG "pad-MOT"
static auto _logger = DAB_LOG_REGISTER(TAG);
#define LOG_MESSAGE(...) DAB_LOG_MESSAGE(TAG, fmt::format(__VA_ARGS__))
#define LOG_ERROR(...) DAB_LOG_ERROR(TAG, fmt::format(__VA_ARGS__))

constexpr size_t TOTAL_CRC_BYTES = 2;
constexpr size_t TOTAL_SEGMENT_HEADER_BYTES = 2;
constexpr size_t MIN_REQUIRED_BYTES = TOTAL_CRC_BYTES + TOTAL_SEGMENT_HEADER_BYTES;

PAD_MOT_Processor::PAD_MOT_Processor() {
    m_data_group.Reset();
    m_state = State::WAIT_LENGTH;

    // DOC: ETSI EN 301 234
    // Clause 5: Structural description
    // Figure 3: Data transfer in DAB using MOT - data flow 
    // TODO: Is the same MOT processor used by different sources in the same service
    // 1. MSC data packet mode service component
    // 2. MSC data stream mode service component
    // 3. PAD via AAC data_stream_element()
    // 4. PAD via MPEG-II
    m_mot_processor = std::make_unique<MOT_Processor>();
}

PAD_MOT_Processor::~PAD_MOT_Processor() = default;

void PAD_MOT_Processor::ProcessXPAD(
    const bool is_start, const bool is_conditional_access,
    tcb::span<const uint8_t> buf) 
{
    const size_t N = buf.size();
    size_t curr_byte = 0;
    bool curr_is_start = is_start;
    while (curr_byte < N) {
        const size_t nb_remain = N-curr_byte;
        const size_t nb_read = Consume(
            curr_is_start, is_conditional_access, 
            {&buf[curr_byte], nb_remain});
        curr_byte += nb_read;
        curr_is_start = false;
    }
}

void PAD_MOT_Processor::SetGroupLength(const uint16_t length) {
    if (m_state != State::WAIT_LENGTH) {
        LOG_ERROR("Overwriting incomplete group length {} to {}", 
            m_data_group.GetRequiredBytes(), length);
    }

    if (length == 0) {
        m_data_group.Reset();
        m_state = State::WAIT_LENGTH;
        return;
    }

    if (length < MIN_REQUIRED_BYTES) {
        LOG_ERROR("Insufficient size for header and crc {}<{}", length, MIN_REQUIRED_BYTES);
        m_data_group.Reset();
        m_state = State::WAIT_LENGTH;
        return;
    } 

    m_data_group.Reset();
    m_data_group.SetRequiredBytes(length);
    m_state = State::WAIT_START;
}

size_t PAD_MOT_Processor::Consume(
    const bool is_start, const bool is_conditional_access,
    tcb::span<const uint8_t> buf) 
{
    const size_t N = buf.size();
    // Wait until we get the corresponding data group length indicator
    // NOTE: We can get null padding bytes which triggers this erroneously
    if (m_state == State::WAIT_LENGTH) {
        return N;
    }

    if ((m_state == State::WAIT_START) && !is_start) {
        return N;
    }

    if (is_start) {
        if ((m_state != State::WAIT_START) && !m_data_group.IsComplete()) {
            LOG_MESSAGE("Discarding partial data group {}/{}", m_data_group.GetCurrentBytes(), m_data_group.GetRequiredBytes());
        }
        m_state = State::READ_DATA;
    }

    const size_t nb_read = m_data_group.Consume({buf.data(), N});
    // TODO: This takes quite a long time for some broadcasters
    //       Signal this data group information to a listener
    LOG_MESSAGE("Progress partial data group {}/{}", m_data_group.GetCurrentBytes(), m_data_group.GetRequiredBytes());
    if (!m_data_group.IsComplete()) {
        return nb_read;
    }

    Interpret();
    m_state = State::WAIT_LENGTH;
    m_data_group.Reset();
    return nb_read;
}


void PAD_MOT_Processor::Interpret(void) {
    const auto buf = m_data_group.GetData();
    const size_t N = m_data_group.GetRequiredBytes();
    const auto res = MSC_Data_Group_Process({buf.data(), N});
    using Status = MSC_Data_Group_Process_Result::Status;
    if (res.status != Status::SUCCESS) {
        return;
    }

    // DOC: ETSI EN 300 401
    // Clause 5.3.3.1 - MSC data group header 
    // Depending on what the MSC data group is used for the header might have certain fields
    // For a MOT (multimedia object transfer) transported via XPAD we need the following:
    // 1. Segment number - So we can reassemble the MOT object
    if (!res.has_segment_field) {
        LOG_ERROR("Missing segment field in MSC XPAD header");
        return;
    }
    // 2. Transport id - So we can identify if a new MOT object is being transmitted
    if (!res.has_transport_id) {
        LOG_ERROR("Missing transport if field in MSC XPAD header");
        return;
    }

    MOT_MSC_Data_Group_Header header;
    header.data_group_type = static_cast<MOT_Data_Type>(res.data_group_type);
    header.continuity_index = res.continuity_index;
    header.repetition_index = res.repetition_index;
    header.is_last_segment = res.segment_field.is_last_segment;
    header.segment_number = res.segment_field.segment_number;
    header.transport_id = res.transport_id;
    m_mot_processor->Process_MSC_Data_Group(header, res.data_field);
}