#include "./pad_processor.h"
#include <stddef.h>
#include <stdint.h>
#include <memory>
#include <string_view>
#include <fmt/format.h>
#include "utility/observable.h"
#include "utility/span.h"
#include "./pad_MOT_processor.h"
#include "./pad_data_length_indicator.h"
#include "./pad_dynamic_label.h"
#include "../dab_logging.h"
#include "../mot/MOT_entities.h"
#include "../mot/MOT_processor.h"
#define TAG "pad-processor"
static auto _logger = DAB_LOG_REGISTER(TAG);
#define LOG_MESSAGE(...) DAB_LOG_MESSAGE(TAG, fmt::format(__VA_ARGS__))
#define LOG_ERROR(...) DAB_LOG_ERROR(TAG, fmt::format(__VA_ARGS__))


constexpr int MAX_XPAD_BYTES = 196;
constexpr int MAX_CI_LENGTH = 4;

// DOC: ETSI EN 300 401
// Clause 7.4.4.2 - Contents indicator in variable size X-PAD 
// The length_index corresponds to the following table of XPAD data lengths
const uint8_t CONTENT_INDICATOR_LENGTH_TABLE[8] = {4, 6, 8, 12, 16, 24, 32, 48};

PAD_Processor::PAD_Processor() {
    m_xpad_unreverse_buf.resize(MAX_XPAD_BYTES);

    // we need to persist the contents indicator list between frames
    // this is because the encoder can choose to exclude them in intermediate packets
    m_ci_list.resize(MAX_CI_LENGTH);
    m_ci_list.resize(0);

    // we need to associate consecutive data length indicators and MOT packets
    m_previous_mot_length = 0;

    m_dynamic_label = std::make_unique<PAD_Dynamic_Label>();
    m_data_length_indicator = std::make_unique<PAD_Data_Length_Indicator>();
    m_pad_mot_processor = std::make_unique<PAD_MOT_Processor>();
}

PAD_Processor::~PAD_Processor() = default;

Observable<std::string_view, const uint8_t>& PAD_Processor::OnLabelUpdate() {
    return m_dynamic_label->OnLabelChange();
}

Observable<uint8_t>& PAD_Processor::OnLabelCommand() {
    return m_dynamic_label->OnCommand();
}

Observable<MOT_Entity>& PAD_Processor::OnMOTUpdate() {
    return m_pad_mot_processor->Get_MOT_Processor().OnEntityComplete();
}

void PAD_Processor::Process(tcb::span<const uint8_t> fpad, tcb::span<const uint8_t> xpad_reversed) {
    // If we have no XPAD, reset the CI list
    // NOTE: Some broadcasters violate this part of the standard and assume the CI list will be preserved
    //       Hence we choose to be lenient and don't reset the CI list
    if (xpad_reversed.empty()) {
        // LOG_MESSAGE("Resetting XPAD on NULL");
        // ci_list_length = 0;
        return;
    }

    if (xpad_reversed.size() > MAX_XPAD_BYTES) {
        LOG_ERROR("XPAD larger than allowable max {}>{}", xpad_reversed.size(), MAX_XPAD_BYTES);
        return;
    }

    if (fpad.size() != 2) {
        LOG_ERROR("FPAD must be 2 bytes got {} bytes", fpad.size());
        return;
    }

    // DOC: ETSI EN 300 401
    // Clause 7.4.1: Coding of F-PAD 
    const uint8_t fpad_type    = (fpad[0] & 0b11000000) >> 6;
    const uint8_t fpad_byte_L0 = (fpad[0] & 0b00111111) >> 0;
    // const uint8_t fpad_byte_L1 = (fpad[1] & 0b11111100) >> 2;
    const uint8_t fpad_CI_flag = (fpad[1] & 0b00000010) >> 1;
    // const uint8_t fpad_Z       = (fpad[1] & 0b00000001) >> 0;

    if (fpad_type != 0b00) {
        LOG_ERROR("FPAD type {} reserved for future use", fpad_type);
        return;
    }

    const uint8_t xpad_indicator = (fpad_byte_L0 & 0b00110000) >> 4;
    const uint8_t xpad_L_type    = (fpad_byte_L0 & 0b00001111) >> 0;
    // const uint8_t xpad_L_data    = fpad_byte_L1;

    if ((xpad_indicator == 0b00) && !xpad_reversed.empty()) {
        LOG_ERROR("Inconsistent NULL xpad information indicator={} xpad_bytes={}", xpad_indicator, xpad_reversed.size());
        return;
    }

    switch (xpad_L_type) {
    // No information or in-house proprietary information
    case 0b0000:
        break;
    // DAB DRC (dynamic range control) field
    case 0b0001:
        // TODO:
        break;
    default:
        LOG_ERROR("Unknown xpad L byte indicator {}", xpad_L_type);
        break;
    }

    // DOC: ETSI EN 300 401
    // Clause 7.4.2.0 Structure of X-PAD (General)
    // NOTE: The byte order of the XPAD is reversed before transmission
    //       The bit order is preserved
    for (size_t i = 0; i < xpad_reversed.size(); i++) {
        m_xpad_unreverse_buf[i] = xpad_reversed[xpad_reversed.size()-1-i];
    }

    auto xpad_data = tcb::span(m_xpad_unreverse_buf).first(xpad_reversed.size());

    switch (xpad_indicator) {
    // No xpad field
    case 0b00:
        if (xpad_data.size() != 0) {
            LOG_ERROR("XPAD indicator indicates no data field but got {} bytes", xpad_data.size());
        }
        break;
    // RFU xpad field
    case 0b11:
        LOG_ERROR("Reserved for future use XPAD indicator {}", xpad_indicator);
        break;
    case 0b01:
        Process_Short_XPAD(xpad_data, fpad_CI_flag);
        break;
    case 0b10:
        Process_Variable_XPAD(xpad_data, fpad_CI_flag);
        break;
    default:
        LOG_ERROR("Unknown xpad indicator {}", xpad_indicator);
        return;
    }

}

void PAD_Processor::Process_Short_XPAD(tcb::span<const uint8_t> xpad, const bool has_indicator_list) {
    // DOC: ETSI EN 300 401
    // Clause 7.4.2.1 - Short XPAD
    // Figure 30: An X-PAD data group extending over three consecutive X-PAD fields 

    // Each short XPAD field is 4 bytes long
    // It is either 1byte CI and 3 bytes data, or 4 bytes data
    const size_t DATA_BYTES_WITH_CI = 3;
    const size_t DATA_BYTES_WITHOUT_CI = 4;

    const size_t N = xpad.size();
    size_t curr_byte = 0;
    if (has_indicator_list) {
        if (N < 1)  {
            LOG_ERROR("[short-xpad] Insufficient length for indicator list {}/{}", 1, N);
            return;
        }
        // DOC: ETSI EN 300 401
        // Clause 7.4.4.1: Contents indicator in short X-PAD 
        // Figure 32: Contents indicator for short X-PAD
        const uint8_t CI = xpad[curr_byte++];
        // const uint8_t rfu      = (CI & 0b11100000) >> 5;
        const uint8_t app_type = (CI & 0b00011111) >> 0;

        const auto indicator = PAD_Content_Indicator{ DATA_BYTES_WITH_CI, app_type };
        m_ci_list.resize(1);
        m_ci_list[0] = indicator;
    }

    if (m_ci_list.empty()) {
        LOG_ERROR("[short-xpad] CI has not been given yet");
        return;
    }

    if (m_ci_list.size() != 1) {
        LOG_ERROR("[short-xpad] CI list length is unexpected for short xpad {} != 1", m_ci_list.size());
        m_ci_list.resize(0);
        return;
    }

    ProcessDataField(xpad.subspan(curr_byte));
    // Proceding data fields don't include the content indicator
    m_ci_list[0].length = DATA_BYTES_WITHOUT_CI;
}

void PAD_Processor::Process_Variable_XPAD(tcb::span<const uint8_t> xpad, const bool has_indicator_list) {
    // DOC: ETSI EN 300 401
    // Clause 7.4.2: Structure of X-PAD 
    // Figure 31: Three X-PAD data groups carried in one X-PAD field
    const size_t N = xpad.size();
    size_t curr_byte = 0;
    if (has_indicator_list) {
        m_ci_list.resize(0);
        for (size_t i = 0; i < MAX_CI_LENGTH; i++) {
            const uint8_t CI = xpad[curr_byte++];

            // DOC: ETSI EN 300 401
            // Clause 7.4.4.2: Contents indicator in variable size X-PAD 
            // Figure 33: Contents indicator for variable size X-PAD
            const uint8_t length_index = (CI & 0b11100000) >> 5;
            const uint8_t app_type     = (CI & 0b00011111) >> 0;

            // DOC: ETSI EN 300 401
            // Clause 7.4.3: Application types
            // Table 11: XPAD Application types
            // 0 = End marker
            if (app_type == 0) {
                break;
            }

            const uint8_t length = CONTENT_INDICATOR_LENGTH_TABLE[length_index];
            const auto indicator = PAD_Content_Indicator{ length, app_type };
            m_ci_list.push_back(indicator);
        }
    } else {
        LOG_ERROR("[var-xpad] No CI list L={}", N);
    }

    ProcessDataField(xpad.subspan(curr_byte));
}

void PAD_Processor::ProcessDataField(tcb::span<const uint8_t> data_field) {
    const int N = (int)data_field.size();
    int curr_byte = 0;
    for (size_t i = 0; i < m_ci_list.size(); i++) {
        auto& content = m_ci_list[i];

        const int nb_remain = N-curr_byte;
        if (content.length > nb_remain) {
            LOG_ERROR("Insufficent length for data field {}/{} i={}/{}", content.length, nb_remain, i, m_ci_list.size());
            return;
        }

        // LOG_MESSAGE("CI={}/{} ci_app={} ci_len={} N={}", 
        //     i, ci_list_length, content.app_type, content.length, N);
        auto data_subfield = data_field.subspan(curr_byte, content.length);

        // DOC: ETSI EN 300 401 
        // Clause 7.4.5.1: MSC data groups in X-PAD 
        // The data group length indicator (type=1) indicates the size of an MSC data group sent via XPAD (type=12,13,14,15)
        // Clause 7.4.5.1.1: X-PAD data group for data group length indicator
        // The data group length covers the data group header, the session header, the data group data field and the optional CRC
        const uint16_t current_mot_length = m_previous_mot_length;
        m_previous_mot_length = 0;

        // NOTE: Sometimes broadcasters send data length indicator groups across two XPAD data fields
        //       These are a 3byte + 4byte data group
        //       The valid bytes for the data length indicators are 3bytes + 1byte, the remaining 3bytes are padding
        //       If we dont remove those 3 padding bytes they will corrupt the data length indicator
        //       Therefore once the data length indicator XPAD data fields are processed, we reset the data group
        if (content.app_type != 1) {
            m_data_length_indicator->ResetLength();
        }

        // NOTE: For application types which have a separate type for the starting and continuation XPAD data field of a data group
        //       We update the content indicator so the app type becomes a continuation XPAD data field
        //       This is because broadcasters can neglect to transmit the CI for consecutive XPAD data fields
        //       This applies to the dynamic label (2->3), MOT (12->13), MOT with conditional access (14->15)

        // DOC: ETSI EN 300 401
        // Clause 7.4.3 - Application types
        // Table 11 - XPAD Application types
        switch (content.app_type) {
        // 0: End marker - Signifies that there is no data in the XPAD field
        case 0:
            break;
        // 1: Data group length indicator for MSC XPAD data group
        case 1: 
            m_data_length_indicator->ProcessXPAD(data_subfield);
            if (m_data_length_indicator->GetIsLengthAvailable()) {
                m_previous_mot_length = m_data_length_indicator->GetLength();
                m_data_length_indicator->ResetLength();
            }
            break;
        // 2: Dynamic label segment start
        // 3: Dynamic label segment continuation
        case 2:
            content.app_type = 3;
            m_dynamic_label->ProcessXPAD(true, data_subfield);
            break;
        case 3:
            m_dynamic_label->ProcessXPAD(false, data_subfield);
            break;
        // DOC: ETSI EN 301 234
        // 12: MOT start
        // 13: MOT continuation
        // 14: MOT start of CA
        // 15: MOT continuation of CA
        case 12:
            content.app_type = 13;
            m_pad_mot_processor->SetGroupLength(current_mot_length);
            m_pad_mot_processor->ProcessXPAD(true, false, data_subfield);
            break;
        case 13:
            m_pad_mot_processor->ProcessXPAD(false, false, data_subfield);
            break;
        case 14:
            content.app_type = 15;
            m_pad_mot_processor->SetGroupLength(current_mot_length);
            m_pad_mot_processor->ProcessXPAD(true, true, data_subfield);
            break;
        case 15:
            m_pad_mot_processor->ProcessXPAD(false, true, data_subfield);
            break;
        default:
            LOG_ERROR("Unsupported app_type={} length={} i={}/{}", 
                content.app_type, content.length, i, m_ci_list.size());
            break;
        }

        curr_byte += content.length;
    }

    // NOTE: This occurs quite often because some broadcasters will pad out unused capacity with NULL bytes
    if (curr_byte != N) {
        // LOG_ERROR("Remaining unconsumed bytes {}/{}", curr_byte, N);
        return;
    }
}

