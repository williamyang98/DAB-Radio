// DOC: ETSI EN 300 401
// Clause 5.2.2.5 - Summary of available FIGs
// Tables 3,4,5 - Provides the associated clause in the document to fig x/x
// All the logic in this file is completely based on the descriptions in these clauses

#include "./fig_processor.h"
#include <stddef.h>
#include <stdint.h>
#include <string_view>
#include <fmt/format.h>
#include "utility/span.h"
#include "./fig_handler_interface.h"
#include "../dab_logging.h"
#define TAG "fig-processor"
static auto _logger = DAB_LOG_REGISTER(TAG);
#define LOG_MESSAGE(...) DAB_LOG_MESSAGE(TAG, fmt::format(__VA_ARGS__))
#define LOG_ERROR(...) DAB_LOG_ERROR(TAG, fmt::format(__VA_ARGS__))

struct ServiceIdentifier {
    uint8_t country_id = 0;
    uint32_t service_reference = 0;
    uint8_t ecc = 0;
    // 2 byte form
    void ProcessShortForm(tcb::span<const uint8_t> b) {
        country_id        =       (b[0] & 0b11110000) >> 4;
        service_reference =
            (static_cast<uint16_t>(b[0] & 0b00001111) << 8) |
                                 ((b[1] & 0b11111111) >> 0);
        ecc = 0;
    }
    // 4 byte form
    void ProcessLongForm(tcb::span<const uint8_t> b) {
        ecc =                     (b[0] & 0b11111111) >> 0;
        country_id =              (b[1] & 0b11110000) >> 4;
        service_reference =
            (static_cast<uint32_t>(b[1] & 0b00001111) << 16) |
            (static_cast<uint32_t>(b[2] & 0b11111111) << 8 ) |
            (static_cast<uint32_t>(b[3] & 0b11111111) << 0 );
    }
};

struct EnsembleIdentifier {
    uint8_t country_id = 0;
    uint16_t ensemble_reference = 0;

    void ProcessBuffer(tcb::span<const uint8_t> buf) {
        country_id =              (buf[0] & 0b11110000) >> 4;
        ensemble_reference = 
            (static_cast<uint16_t>(buf[0] & 0b00001111) << 8) |
                                 ((buf[1] & 0b11111111) >> 0);
    }

    void ProcessU16(const uint16_t data) {
        country_id =         (data & 0xF000) >> 12;
        ensemble_reference = (data & 0x0FFF) >> 0;
    }
};

// DOC: ETSI EN 300 401
// Clause 5.2: Fast Information Channel (FIC) 
// Clause 5.2.1: Fast Information Block (FIB) 
// A FIB (fast information block) contains many FIGs (fast information groups)
void FIG_Processor::ProcessFIB(tcb::span<const uint8_t> buf) {
    // Dont do anything if we don't have an associated handler
    if (m_handler == nullptr) {
        return;
    }

    const int N = (int)buf.size();

    int curr_byte = 0;
    while (curr_byte < N) {
        const int nb_remain_bytes = N-curr_byte;

        // DOC: ETSI EN 300 401
        // Clause 5.2.2.0: Introduction 
        // Figure 6: Structure of the FIB 
        // Table 2: List of FIG types 

        const uint8_t header = buf[curr_byte];
        // delimiter byte
        if (header == 0xFF) {
            return;
        }

        const uint8_t fig_type =              (header & 0b11100000) >> 5;
        const uint8_t fig_data_length_bytes = (header & 0b00011111) >> 0;

        const uint8_t fig_length_bytes = fig_data_length_bytes+1;

        if (fig_length_bytes > nb_remain_bytes) {
            LOG_ERROR("fig specified length overflows buffer ({}/{})",
                fig_length_bytes, nb_remain_bytes);
            return;
        }

        const auto fig_buf = buf.subspan(curr_byte+1, fig_data_length_bytes);
        curr_byte += fig_length_bytes;

        switch (fig_type) {
        // MCI and part of SI
        case 0: ProcessFIG_Type_0(fig_buf); break;
        // Labels etc. part of SI
        case 1: ProcessFIG_Type_1(fig_buf); break;
        // Labels etc. part of SI
        case 2: ProcessFIG_Type_2(fig_buf); break;
        // Conditional access
        case 6: ProcessFIG_Type_6(fig_buf); break;
        // Ending byte of the FIG packet
        // If data occupying all 30 bytes, no delimiter present
        // If data occupying less than 30 bytes, delimiter present and any 0x00 padding afterwards
        case 7:
            curr_byte = N;
            break;
        // reserved 
        case 3:
        case 4:
        case 5:
        default:
            LOG_ERROR("Invalid fig type ({})", fig_type);
            return;
        }
    }
}

void FIG_Processor::ProcessFIG_Type_0(tcb::span<const uint8_t> buf) {
    if (buf.empty()) {
        LOG_ERROR("Received an empty fig 0/x buffer");
        return;
    }

    // DOC: ETSI EN 300 401
    // Clause 5.2.2.1: MCI and SI: FIG type 0 data field 
    // Figure 7: Structure of the FIG type 0 data field 
    const uint8_t descriptor = buf[0];
    FIG_Header_Type_0 header;
    header.cn               = (descriptor & 0b10000000) >> 7;
    header.oe               = (descriptor & 0b01000000) >> 6;
    header.pd               = (descriptor & 0b00100000) >> 5;
    const uint8_t extension = (descriptor & 0b00011111) >> 0;

    auto field_buf = buf.subspan(1);

    switch (extension) {
    case 0 : ProcessFIG_Type_0_Ext_0 (header, field_buf); break;
    case 1 : ProcessFIG_Type_0_Ext_1 (header, field_buf); break;
    case 2 : ProcessFIG_Type_0_Ext_2 (header, field_buf); break;
    case 3 : ProcessFIG_Type_0_Ext_3 (header, field_buf); break;
    case 4 : ProcessFIG_Type_0_Ext_4 (header, field_buf); break;
    case 5 : ProcessFIG_Type_0_Ext_5 (header, field_buf); break;
    case 6 : ProcessFIG_Type_0_Ext_6 (header, field_buf); break;
    case 7 : ProcessFIG_Type_0_Ext_7 (header, field_buf); break;
    case 8 : ProcessFIG_Type_0_Ext_8 (header, field_buf); break;
    case 9 : ProcessFIG_Type_0_Ext_9 (header, field_buf); break;
    case 10: ProcessFIG_Type_0_Ext_10(header, field_buf); break;
    case 13: ProcessFIG_Type_0_Ext_13(header, field_buf); break;
    case 14: ProcessFIG_Type_0_Ext_14(header, field_buf); break;
    case 17: ProcessFIG_Type_0_Ext_17(header, field_buf); break;
    case 21: ProcessFIG_Type_0_Ext_21(header, field_buf); break;
    case 24: ProcessFIG_Type_0_Ext_24(header, field_buf); break;
    default:
        LOG_MESSAGE("fig 0/{} Unsupported", extension);
        break;
    }
}

void FIG_Processor::ProcessFIG_Type_1(tcb::span<const uint8_t> buf) {
    if (buf.empty()) {
        LOG_ERROR("Received an empty fig 1/x buffer");
        return;
    }

    // DOC: ETSI EN 300 401
    // Clause 5.2.2.2: Labels: FIG type 1 data field 
    // Figure 8: Structure of the FIG type 1 data field 
    const uint8_t descriptor = buf[0];
    FIG_Header_Type_1 header;
    header.charset          = (descriptor & 0b11110000) >> 4;
    header.rfu              = (descriptor & 0b00001000) >> 3;
    const uint8_t extension = (descriptor & 0b00000111) >> 0;

    auto field_buf = buf.subspan(1);

    switch (extension) {
    case 0: ProcessFIG_Type_1_Ext_0(header, field_buf); break;
    case 1: ProcessFIG_Type_1_Ext_1(header, field_buf); break;
    case 4: ProcessFIG_Type_1_Ext_4(header, field_buf); break;
    case 5: ProcessFIG_Type_1_Ext_5(header, field_buf); break;
    default:
        LOG_MESSAGE("fig 1/{} L={} Unsupported", extension, field_buf.size());
        break;
    }
}

void FIG_Processor::ProcessFIG_Type_2(tcb::span<const uint8_t> buf) {
    if (buf.empty()) {
        LOG_ERROR("Received an empty fig 2/x buffer");
        return;
    }

    const uint8_t descriptor = buf[0];
    // FIG_Header_Type_2 header;
    // header.toggle_flag      = (descriptor & 0b10000000) >> 7;
    // header.segment_index    = (descriptor & 0b01110000) >> 4;
    // header.rfu              = (descriptor & 0b00001000) >> 3;
    const uint8_t extension = (descriptor & 0b00000111) >> 0;

    auto field_buf = buf.subspan(1);
    LOG_MESSAGE("fig 2/{} L={} Unsupported", extension, field_buf.size());
}

void FIG_Processor::ProcessFIG_Type_6(tcb::span<const uint8_t> buf) {
    if (buf.empty()) {
        LOG_ERROR("Received an empty fig 6/x buffer");
        return;
    }

    // const uint8_t descriptor = buf[0];
    // const uint8_t rfu              = (descriptor & 0b10000000) >> 7;
    // const uint8_t cn               = (descriptor & 0b01000000) >> 6;
    // const uint8_t oe               = (descriptor & 0b00100000) >> 5;
    // const uint8_t pd               = (descriptor & 0b00010000) >> 4;
    // const uint8_t lef              = (descriptor & 0b00001000) >> 3;
    // const uint8_t short_CA_sys_id  = (descriptor & 0b00000111) >> 0;

    auto field_buf = buf.subspan(1);
    LOG_MESSAGE("fig 6 L={} Unsupported", field_buf.size());
}

// Ensemble information
void FIG_Processor::ProcessFIG_Type_0_Ext_0(
    const FIG_Header_Type_0 header, 
    tcb::span<const uint8_t> buf)
{
    const int N = (int)buf.size();
    const int nb_field_bytes = 4;
    if (N != nb_field_bytes) {
        LOG_ERROR("fig 0/0 Length doesn't match expectations ({}/{})",
            nb_field_bytes, N);
        return;
    }

    const int nb_eid_bytes = 2;
    EnsembleIdentifier eid;
    eid.ProcessBuffer({buf.data(), (size_t)nb_eid_bytes});
    
    const uint8_t change_flags = (buf[2] & 0b11000000) >> 6;
    const uint8_t alarm_flag =   (buf[2] & 0b00100000) >> 5;

    // CIF mod 5000 counter
    // mod 20 counter
    const uint8_t cif_upper =    (buf[2] & 0b00011111) >> 0;
    // mod 250 counter
    const uint8_t cif_lower =    (buf[3] & 0b11111111) >> 0;

    // TODO: For some reason we don't get this byte
    // Perhaps it is because no changes occur, but this isn't stated in standard
    // const uint8_t occurance_change = 
    //                              (buf[4] & 0b11111111) >> 0;

    LOG_MESSAGE("fig 0/0 country_id={} ensemble_ref={} change={} alarm={} cif={}|{}",
        eid.country_id, eid.ensemble_reference,
        change_flags, alarm_flag,
        cif_upper, cif_lower);
    
    m_handler->OnEnsemble_1_ID(
        eid.country_id, eid.ensemble_reference,
        change_flags, alarm_flag, 
        cif_upper, cif_lower);
}

// Subchannel for stream mode MSC
void FIG_Processor::ProcessFIG_Type_0_Ext_1(const FIG_Header_Type_0 header, tcb::span<const uint8_t> buf) {
    const int N = (int)buf.size();
    int curr_byte = 0;
    int curr_subchannel = 0;
    while (curr_byte < N) {
        auto* data = &buf[curr_byte];
        const uint8_t nb_remain = N-curr_byte;

        // Minimum length of header
        if (nb_remain < 3) {
            LOG_ERROR("fig 0/1 Ended early for some reason ({})", curr_byte);
            break;
        }
        
        const uint8_t subchannel_id = (data[0] & 0b11111100) >> 2;
        const uint16_t start_address =
                (static_cast<uint16_t>(data[0] & 0b00000011) << 8) |
                                     ((data[1] & 0b11111111) >> 0);

        const uint8_t is_long_form = (data[2] & 0b10000000) >> 7;
        const uint8_t nb_data_bytes = is_long_form ? 4 : 3;
        if (nb_data_bytes > nb_remain) {
            LOG_ERROR("fig 0/1 Long field cannot fit in remaining length");
            break;
        }

        // process short form
        // this provides configuration on Unequal Error Protection
        if (!is_long_form) {
            const uint8_t table_switch = (data[2] & 0b01000000) >> 6;
            const uint8_t table_index  = (data[2] & 0b00111111) >> 0;

            LOG_MESSAGE("fig 0/1 i={} subchannel_id={:>2} start_addr={:>3} long={} table_switch={} table_index={}",
                curr_subchannel,
                subchannel_id, start_address, is_long_form,
                table_switch, table_index);

            m_handler->OnSubchannel_1_Short(
                subchannel_id, start_address,
                table_switch, table_index);
        // process long form
        // this provides configuration for Equal Error Protection
        } else {
            const uint8_t option       = (data[2] & 0b01110000) >> 4;
            const uint8_t prot_level   = (data[2] & 0b00001100) >> 2;
            const uint16_t subchannel_size = 
                   (static_cast<uint16_t>(data[2] & 0b00000011) << 8) |
                                        ((data[3] & 0b11111111) >> 0);

            LOG_MESSAGE("fig 0/1 i={} subchannel_id={:>2} start_addr={:>3} long={} option={} prot_level={} subchannel_size={}",
                curr_subchannel,
                subchannel_id, start_address, is_long_form,
                option, prot_level, subchannel_size);

            m_handler->OnSubchannel_1_Long(
                subchannel_id, start_address,
                option, prot_level, subchannel_size);
        }
        curr_byte += nb_data_bytes;
        curr_subchannel++;
    }
}

// Service and service components information in stream mode
void FIG_Processor::ProcessFIG_Type_0_Ext_2(const FIG_Header_Type_0 header, tcb::span<const uint8_t> buf) {
    const int N = (int)buf.size();
    const int nb_service_id_bytes = header.pd ? 4 : 2;
    // In addition to the service id field, we have an additional byte of fields
    const int nb_header_bytes = nb_service_id_bytes+1;

    int curr_index = 0;
    int curr_service = 0;
    while (curr_index < N) {
        // Get the service data
        auto* service_buf = &buf[curr_index];
        const int nb_remain_bytes = N-curr_index;

        if (nb_header_bytes > nb_remain_bytes) {
            LOG_ERROR("fig 0/2 Message not long enough header field for service data");
            return;
        }

        ServiceIdentifier sid;
        if (!header.pd) {
            sid.ProcessShortForm({service_buf, (size_t)nb_service_id_bytes});
        } else {
            sid.ProcessLongForm({service_buf, (size_t)nb_service_id_bytes});
        }

        const uint8_t descriptor = service_buf[nb_service_id_bytes];
        // const uint8_t rfa                   = (descriptor & 0b10000000) >> 7;
        // const uint8_t CAId                  = (descriptor & 0b01110000) >> 4;
        const uint8_t nb_service_components = (descriptor & 0b00001111) >> 0;

        // Determine if we have enough bytes for the service components data
        const uint8_t nb_service_component_bytes = 2;
        const int nb_length_bytes = nb_service_component_bytes*nb_service_components + nb_header_bytes;

        if (nb_length_bytes > nb_remain_bytes) {
            LOG_ERROR("fig 0/2 Message not long enough for service components");
            return;
        }

        auto* components_buf = &service_buf[nb_header_bytes];

        // NOTE: To determine the SCIdS (service component id within service)
        // Refer to clause 6.3.7.1 of EN 300 401
        // It states that we should correlate the service id and the subchannel id 
        // This is done by getting the SCIdS/subchannel_id pairing from fig 0/8

        // Get all the components
        for (int i = 0; i < nb_service_components; i++) {
            auto* b = &components_buf[i*nb_service_component_bytes];
            const uint8_t b0 = b[0];
            const uint8_t b1 = b[1];

            const uint8_t tmid = (b0 & 0b11000000) >> 6;
            switch (tmid) {
            // MSC stream audio
            case 0b00:
                {
                    const uint8_t ASTCy         = (b0 & 0b00111111) >> 0;
                    const uint8_t subchannel_id = (b1 & 0b11111100) >> 2;
                    const uint8_t is_primary    = (b1 & 0b00000010) >> 1;
                    const uint8_t ca_flag       = (b1 & 0b00000001) >> 0;
                    LOG_MESSAGE("fig 0/2 pd={} country_id={:>2} service_ref={:>4} ecc={} i={}-{}/{} tmid={} ASTCy={} subchannel_id={:>2} ps={} ca={}",
                        header.pd,
                        sid.country_id, sid.service_reference, sid.ecc,
                        curr_service, i, nb_service_components,
                        tmid, 
                        ASTCy, subchannel_id, is_primary, ca_flag);
                    
                    m_handler->OnServiceComponent_1_StreamAudioType(
                        sid.country_id, sid.service_reference, sid.ecc,
                        subchannel_id, ASTCy, is_primary);
                }
                break;
            // MSC stream data
            case 0b01:
                {
                    const uint8_t DSCTy         = (b0 & 0b00111111) >> 0;
                    const uint8_t subchannel_id = (b1 & 0b11111100) >> 2;
                    const uint8_t is_primary    = (b1 & 0b00000010) >> 1;
                    const uint8_t ca_flag       = (b1 & 0b00000001) >> 0;
                    LOG_MESSAGE("fig 0/2 pd={} country_id={:>2} service_ref={:>4} ecc={} i={}-{}/{} tmid={} DSTCy={} subchannel_id={:>2} ps={} ca={}",
                        header.pd,
                        sid.country_id, sid.service_reference, sid.ecc,
                        curr_service, i, nb_service_components,
                        tmid, 
                        DSCTy, subchannel_id, is_primary, ca_flag);
                    
                    m_handler->OnServiceComponent_1_StreamDataType(
                        sid.country_id, sid.service_reference, sid.ecc,
                        subchannel_id, DSCTy, is_primary);
                }
                break;
            // MSC packet data
            case 0b11:
                {
                    // service component indentifier
                    const uint16_t SCId = 
                            (static_cast<uint16_t>(b0 & 0b00111111) << 6) |
                                                ((b1 & 0b11111100) >> 2);
                    const uint8_t is_primary    = (b1 & 0b00000010) >> 1;
                    const uint8_t ca_flag       = (b1 & 0b00000001) >> 0;
                    LOG_MESSAGE("fig 0/2 pd={} country_id={:>2} service_ref={:>4} ecc={} i={}-{}/{} tmid={} SCId={} ps={} ca={}",
                        header.pd,
                        sid.country_id, sid.service_reference, sid.ecc,
                        curr_service, i, nb_service_components,
                        tmid, 
                        SCId, is_primary, ca_flag);

                    m_handler->OnServiceComponent_1_PacketDataType(
                        sid.country_id, sid.service_reference, sid.ecc,
                        SCId, is_primary);
                }
                break;
            default:
                LOG_ERROR("fig 0/2 reserved tmid={}", tmid);
                return;
            }
        }

        // Move to the next service
        curr_index += nb_length_bytes;
        curr_service++;
    }
}

// Service components information in packet mode
void FIG_Processor::ProcessFIG_Type_0_Ext_3(const FIG_Header_Type_0 header, tcb::span<const uint8_t> buf) {
    const int N = (int)buf.size();
    const int nb_header_bytes = 5;
    const int nb_CAOrg_field_bytes = 2;

    int curr_byte = 0;
    int curr_component = 0;
    while (curr_byte < N) {
        const int nb_bytes_remain = N-curr_byte;
        if (nb_header_bytes > nb_bytes_remain) {
            LOG_ERROR("fig 0/3 Insufficient length for header ({}/{})",
                nb_header_bytes, nb_bytes_remain);
            return;
        }

        auto* b = &buf[curr_byte];
        const uint16_t SCId = 
                (static_cast<uint16_t>(b[0] & 0b11111111) << 4) |
                                     ((b[1] & 0b11110000) >> 4);
        const uint8_t rfa =           (b[1] & 0b00001110) >> 1;
        const uint8_t CAOrg_flag =    (b[1] & 0b00000001) >> 0;
        const uint8_t dg_flag =       (b[2] & 0b10000000) >> 7;
        const uint8_t rfu =           (b[2] & 0b01000000) >> 6;
        const uint8_t DSCTy =         (b[2] & 0b00111111) >> 0;
        const uint8_t subchannel_id = (b[3] & 0b11111100) >> 2;
        const uint16_t packet_address = 
                (static_cast<uint16_t>(b[3] & 0b00000011) << 8) |
                                      (b[4] & 0b11111111) >> 0;

        // CAOrg field is present if CAOrg_flag is set
        uint16_t CAOrg = 0;
        const int nb_data_length = CAOrg_flag ? (nb_header_bytes+nb_CAOrg_field_bytes) : nb_header_bytes;

        if (nb_data_length > nb_bytes_remain) {
            LOG_ERROR("fig 0/3 Insufficient length for CAOrg field ({}/{})",
                nb_data_length, nb_bytes_remain);
            return;
        }

        if (CAOrg_flag) {
            auto* v = &b[nb_header_bytes];
            CAOrg = 
                (static_cast<uint16_t>(v[0] & 0b11111111) << 8) |
                                     ((v[1] & 0b11111111) >> 0);
        }

        LOG_MESSAGE("fig 0/3 i={} SCId={} rfa={} CAOrg={} dg={} rfu={} DSCTy={} subchannel_id={} packet_address={} CAOrg={}", 
            curr_component,
            SCId, rfa, CAOrg_flag, dg_flag, rfu, DSCTy, 
            subchannel_id, packet_address, CAOrg);
        
        m_handler->OnServiceComponent_2_PacketDataType(
            SCId, subchannel_id, DSCTy, packet_address);
        
        curr_byte += nb_data_length;
        curr_component++;
    }
}

// Service components information in stream mode with conditional access
void FIG_Processor::ProcessFIG_Type_0_Ext_4(
    const FIG_Header_Type_0 header, 
    tcb::span<const uint8_t> buf)
{
    const int N = (int)buf.size();
    const int nb_component_bytes = 3;
    if ((N % nb_component_bytes) != 0) {
        LOG_ERROR("fig 0/4 Field must be a multiple of {} bytes", 
            nb_component_bytes);
        return;
    }

    const int nb_components = N / nb_component_bytes;
    for (int i = 0; i < nb_components; i++) {
        auto* b = &buf[i*nb_component_bytes];
        const uint8_t rfa           = (b[0] & 0b10000000) >> 7;
        const uint8_t rfu           = (b[0] & 0b01000000) >> 6;
        const uint8_t subchannel_id = (b[0] & 0b00111111) >> 0;
        const uint16_t CAOrg =
                (static_cast<uint16_t>(b[1] & 0b11111111) << 8) |
                                     ((b[2] & 0b11111111) >> 0);
        LOG_MESSAGE("fig 0/4 i={}/{} rfa={} rfu={} subchannel_id={} CAOrg={}",
            i, nb_components,
            rfa, rfu, subchannel_id, CAOrg);
        
        m_handler->OnServiceComponent_2_StreamConditionalAccess(
            subchannel_id, CAOrg);
    }
}

// Service component language 
void FIG_Processor::ProcessFIG_Type_0_Ext_5(const FIG_Header_Type_0 header, tcb::span<const uint8_t> buf) {
    const int N = (int)buf.size();
    int curr_byte = 0;
    while (curr_byte < N) {
        const int nb_remain_bytes = N-curr_byte;
        auto* b = &buf[curr_byte];

        // Long or short form flag
        const uint8_t LS =  (b[0] & 0b10000000) >> 7;
        const int nb_length_bytes = LS ? 3 : 2;

        if (nb_length_bytes > nb_remain_bytes) {
            LOG_ERROR("fig 0/5 LS={} Insufficient length for contents ({}/{})",
                LS, 
                nb_length_bytes, nb_remain_bytes);
            return;
        }

        // short form
        if (!LS) {
            const uint8_t Rfu =             (b[0] & 0b01000000) >> 6;
            const uint8_t subchannel_id =   (b[0] & 0b00111111) >> 0;
            const uint8_t language = b[1];
            LOG_MESSAGE("fig 0/5 LS={} Rfu={} subchannel_id={:>2} language={}",
                LS, 
                Rfu, subchannel_id, language);

            m_handler->OnServiceComponent_3_Short_Language(
                subchannel_id, language);
        // long form
        } else {
            const uint8_t Rfa =             (b[0] & 0b01110000) >> 4;
            const uint16_t SCId =            
                      (static_cast<uint16_t>(b[0] & 0b00001111) << 8) |
                                           ((b[1] & 0b11111111) >> 0);
            const uint8_t language = b[2];
            LOG_MESSAGE("fig 0/5 LS={} Rfa={} SCId={} language={}",
                LS, 
                Rfa, SCId, language);

            m_handler->OnServiceComponent_3_Long_Language(
                SCId, language); 
        }

        curr_byte += nb_length_bytes;
    }
}

// Service linking information
void FIG_Processor::ProcessFIG_Type_0_Ext_6(const FIG_Header_Type_0 header, tcb::span<const uint8_t> buf) {
    const int N = (int)buf.size();
    const int nb_header_bytes = 2;

    int curr_byte = 0;
    while (curr_byte < N) {
        const int nb_remain_bytes = N-curr_byte;

        // minimum of 16 bits = 2 bytes
        if (nb_remain_bytes < nb_header_bytes) {
            LOG_ERROR("fig 0/6 Insufficient length for header ({}/{})",
                nb_header_bytes, nb_remain_bytes);
            return;
        }

        auto* b = &buf[curr_byte];

        const uint8_t id_list_flag =     (b[0] & 0b10000000) >> 7;
        const uint8_t is_active_link =   (b[0] & 0b01000000) >> 6;
        const uint8_t is_hard_link =     (b[0] & 0b00100000) >> 5;
        const uint8_t is_international = (b[0] & 0b00010000) >> 4;
        const uint16_t linkage_set_number = 
                   (static_cast<uint16_t>(b[0] & 0b00001111) << 8) |
                                        ((b[1] & 0b11111111) >> 0);

        // short data field without id list
        if (!id_list_flag) {
            LOG_MESSAGE("fig 0/6 pd={} ld={} LA={} S/H={} ILS={} LSN={}",
                header.pd,
                id_list_flag, is_active_link, is_hard_link, is_international, linkage_set_number);
            
            m_handler->OnServiceLinkage_1_LSN_Only(
                is_active_link, is_hard_link, is_international, 
                linkage_set_number);
            
            curr_byte += nb_header_bytes;
            continue;
        }

        // id list is present
        // it must contain at least a list header byte
        const int nb_list_header_bytes = 1;
        const int nb_total_header_bytes = nb_header_bytes + nb_list_header_bytes;

        if (nb_remain_bytes < nb_total_header_bytes) {
            LOG_ERROR("fig 0/6 Insufficient length for long header ({}/{})",
                nb_total_header_bytes, nb_remain_bytes);
            return;
        }

        const uint8_t rfu0   = (b[2] & 0b10000000) >> 7;
        const uint8_t IdLQ   = (b[2] & 0b01100000) >> 5;
        const uint8_t Rfa0   = (b[2] & 0b00010000) >> 4;
        const uint8_t nb_ids = (b[2] & 0b00001111) >> 0;

        const int nb_list_remain = nb_remain_bytes-nb_total_header_bytes;
        if (nb_list_remain <= 0) {
            LOG_ERROR("fig 0/6 Insufficient length for any list buffer");
            return;
        }

        // 3 possible arrangements for id list
        auto* list_buf = &b[3];

        // Arrangement 1: List of 16bit IDs
        if (!header.pd && !is_international) {
            const int nb_id_bytes = 2;
            const int nb_list_bytes = nb_id_bytes*nb_ids;
            if (nb_list_bytes > nb_list_remain) {
                LOG_ERROR("fig 0/6 Insufficient length for type 1 id list ({}/{})",
                    nb_list_bytes, nb_list_remain);
                return;
            }

            for (int i = 0; i < nb_ids; i++)  {
                auto* entry_buf = &list_buf[i*nb_id_bytes];

                // Interpret id according to value of IdLQ (id list qualifier) 
                switch (IdLQ) {
                case 0b00: // DAB service id - 16bit
                    {
                        ServiceIdentifier sid;
                        sid.ProcessShortForm({entry_buf, (size_t)2});
                        LOG_MESSAGE("fig 0/6 pd={} ld={} LA={} S/H={} ILS={} LSN={} rfu0={} IdLQ={} Rfa0={} type=1 i={}/{} country_id={} service_ref={} ecc={}",
                            header.pd,
                            id_list_flag, is_active_link, is_hard_link, is_international, linkage_set_number,
                            rfu0, IdLQ, Rfa0, 
                            i, nb_ids,
                            sid.country_id, sid.service_reference, sid.ecc);

                        m_handler->OnServiceLinkage_1_ServiceID(
                            is_active_link, is_hard_link, is_international,
                            linkage_set_number, 
                            sid.country_id, sid.service_reference, sid.ecc);
                    }
                    break;
                case 0b01: // RDS-PI code 
                    {
                        const uint16_t rds_pi_code = 
                            (static_cast<uint16_t>(entry_buf[0]) << 8) |
                                                  (entry_buf[1]  << 0);
                        LOG_MESSAGE("fig 0/6 pd={} ld={} LA={} S/H={} ILS={} LSN={} rfu0={} IdLQ={} Rfa0={} type=1 i={}/{} RDS_PI={:04X}",
                            header.pd,
                            id_list_flag, is_active_link, is_hard_link, is_international, linkage_set_number,
                            rfu0, IdLQ, Rfa0, 
                            i, nb_ids,
                            rds_pi_code);
                        
                        m_handler->OnServiceLinkage_1_RDS_PI_ID(
                            is_active_link, is_hard_link, is_international,
                            linkage_set_number, rds_pi_code);
                    }
                    break;
                case 0b11: // DRM 24bit-service identifier
                    {
                        const uint32_t drm_id = 
                            (static_cast<uint32_t>(entry_buf[0]) << 8) |
                                                  (entry_buf[1]  << 0);
                        LOG_MESSAGE("fig 0/6 pd={} ld={} LA={} S/H={} ILS={} LSN={} rfu0={} IdLQ={} Rfa0={} type=1 i={}/{} DRM_id={}",
                            header.pd,
                            id_list_flag, is_active_link, is_hard_link, is_international, linkage_set_number,
                            rfu0, IdLQ, Rfa0, 
                            i, nb_ids,
                            drm_id);
                        
                        m_handler->OnServiceLinkage_1_DRM_ID(
                            is_active_link, is_hard_link, is_international,
                            linkage_set_number, drm_id);
                    }
                    break;
                default:
                    // Reserved for future use
                    break;
                }
                
            }

            curr_byte += (nb_total_header_bytes + nb_list_bytes);
            continue;
        } 

        // Arrangement 2: List of pairs of (8bit ECC and 16bit ID)
        if (!header.pd && is_international) {
            const int nb_entry_bytes = 3;
            const int nb_list_bytes = nb_entry_bytes*nb_ids;
            if (nb_list_bytes > nb_list_remain) {
                LOG_ERROR("fig 0/6 Insufficient length for type 2 id list ({}/{})",
                    nb_list_bytes, nb_list_remain);
                return;
            }

            for (int i = 0; i < nb_ids; i++)  {
                auto* entry_buf = &list_buf[i*nb_entry_bytes];
                const uint8_t ecc = entry_buf[0];

                // Interpret id according to value of IdLQ (id list qualifier) 
                switch (IdLQ) {
                case 0b00: // DAB service id - 16bit with ecc provided separately
                    {
                        ServiceIdentifier sid;
                        sid.ProcessShortForm({&entry_buf[1], (size_t)2});
                        sid.ecc = ecc;
                        LOG_MESSAGE("fig 0/6 pd={} ld={} LA={} S/H={} ILS={} LSN={} rfu0={} IdLQ={} Rfa0={} type=2 i={}/{} country_id={} service_ref={} ecc={}",
                            header.pd,
                            id_list_flag, is_active_link, is_hard_link, is_international, linkage_set_number,
                            rfu0, IdLQ, Rfa0, 
                            i, nb_ids,
                            sid.country_id, sid.service_reference, sid.ecc);
                        
                        m_handler->OnServiceLinkage_1_ServiceID(
                            is_active_link, is_hard_link, is_international,
                            linkage_set_number,
                            sid.country_id, sid.service_reference, sid.ecc);
                    }
                    break;
                case 0b01: // RDS-PI code with ecc
                    {
                        const uint16_t rds_pi_code = 
                            (static_cast<uint16_t>(entry_buf[1]) << 8) |
                                                  (entry_buf[2]  << 0);
                        LOG_MESSAGE("fig 0/6 pd={} ld={} LA={} S/H={} ILS={} LSN={} rfu0={} IdLQ={} Rfa0={} type=2 i={}/{} RDS_PI={:04X} ecc={}",
                            header.pd,
                            id_list_flag, is_active_link, is_hard_link, is_international, linkage_set_number,
                            rfu0, IdLQ, Rfa0, 
                            i, nb_ids,
                            rds_pi_code, ecc);
                        
                        m_handler->OnServiceLinkage_1_RDS_PI_ID(
                            is_active_link, is_hard_link, is_international,
                            linkage_set_number, 
                            rds_pi_code, ecc);
                    }
                    break;
                case 0b11: // DRM 24bit service identifier with ecc as MSB
                    {
                        const uint32_t drm_id = 
                            (static_cast<uint32_t>(ecc)          << 16) |
                            (static_cast<uint32_t>(entry_buf[1]) << 8) |
                                                  (entry_buf[2]  << 0);
                        LOG_MESSAGE("fig 0/6 pd={} ld={} LA={} S/H={} ILS={} LSN={} rfu0={} IdLQ={} Rfa0={} type=2 i={}/{} DRM_id={}",
                            header.pd,
                            id_list_flag, is_active_link, is_hard_link, is_international, linkage_set_number,
                            rfu0, IdLQ, Rfa0, 
                            i, nb_ids,
                            drm_id);
                        
                        m_handler->OnServiceLinkage_1_DRM_ID(
                            is_active_link, is_hard_link, is_international,
                            linkage_set_number, drm_id);
                    }
                    break;
                default:
                    // Reserved for future use
                    break;
                }
            }

            curr_byte += (nb_total_header_bytes + nb_list_bytes);
            continue;
        } 

        // Arrangement 3: List of 32bit IDs
        {
            const int nb_entry_bytes = 4;
            const int nb_list_bytes = nb_entry_bytes*nb_ids;
            if (nb_list_bytes > nb_list_remain) {
                LOG_ERROR("fig 0/6 Insufficient length for type 3 id list ({}/{})",
                    nb_list_bytes, nb_list_remain);
                return;
            }
            for (int i = 0; i < nb_ids; i++)  {
                auto* entry_buf = &list_buf[i*nb_entry_bytes];

                // Interpret id according to value of IdLQ (id list qualifier) 
                switch (IdLQ) {
                case 0b00: // DAB service id - 32bit 
                    {
                        ServiceIdentifier sid;
                        sid.ProcessLongForm({entry_buf, (size_t)4});
                        LOG_MESSAGE("fig 0/6 pd={} ld={} LA={} S/H={} ILS={} LSN={} rfu0={} IdLQ={} Rfa0={} type=3 i={}/{} country_id={} service_ref={} ecc={}",
                            header.pd,
                            id_list_flag, is_active_link, is_hard_link, is_international, linkage_set_number,
                            rfu0, IdLQ, Rfa0, 
                            i, nb_ids,
                            sid.country_id, sid.service_reference, sid.ecc);
                        
                        m_handler->OnServiceLinkage_1_ServiceID(
                            is_active_link, is_hard_link, is_international,
                            linkage_set_number, 
                            sid.country_id, sid.service_reference, sid.ecc);
                    }
                    break;
                case 0b01: // RDS-PI code 
                    {
                        // const uint32_t id = 
                        //     (static_cast<uint32_t>(entry_buf[0]) << 24) |
                        //     (static_cast<uint32_t>(entry_buf[1]) << 16) |
                        //     (static_cast<uint32_t>(entry_buf[2]) << 8 ) |
                        //                           (entry_buf[3]  << 0 );
                        // TODO: Figure out how the RDSPI code is interpreted in the 32bit field
                        const uint16_t rds_pi_code = 
                              (static_cast<uint16_t>(entry_buf[2]) << 8) |
                                                    (entry_buf[3]  << 0);
                        LOG_MESSAGE("fig 0/6 pd={} ld={} LA={} S/H={} ILS={} LSN={} rfu0={} IdLQ={} Rfa0={} type=3 i={}/{} RDS_PI={:08X}",
                            header.pd,
                            id_list_flag, is_active_link, is_hard_link, is_international, linkage_set_number,
                            rfu0, IdLQ, Rfa0, 
                            i, nb_ids,
                            rds_pi_code);
                        
                        m_handler->OnServiceLinkage_1_RDS_PI_ID(
                            is_active_link, is_hard_link, is_international,
                            linkage_set_number, rds_pi_code);
                    }
                    break;
                case 0b11: // DRM 24bit service identifier
                    {
                        // DRM service id
                        const uint32_t drm_id = 
                            (static_cast<uint32_t>(entry_buf[0]) << 24) |
                            (static_cast<uint32_t>(entry_buf[1]) << 16) |
                            (static_cast<uint32_t>(entry_buf[2]) << 8 ) |
                                                  (entry_buf[3]  << 0 );
                        LOG_MESSAGE("fig 0/6 pd={} ld={} LA={} S/H={} ILS={} LSN={} rfu0={} IdLQ={} Rfa0={} type=3 i={}/{} DRM_id={}",
                            header.pd,
                            id_list_flag, is_active_link, is_hard_link, is_international, linkage_set_number,
                            rfu0, IdLQ, Rfa0, 
                            i, nb_ids,
                            drm_id);
                        
                        m_handler->OnServiceLinkage_1_DRM_ID(
                            is_active_link, is_hard_link, is_international,
                            linkage_set_number, drm_id);
                    }
                    break;
                default:
                    // Reserved for future use
                    break;
                }
            }

            curr_byte += (nb_total_header_bytes + nb_list_bytes);
            continue;
        }
    }
}

// Configuration information
void FIG_Processor::ProcessFIG_Type_0_Ext_7(const FIG_Header_Type_0 header, tcb::span<const uint8_t> buf) {
    const int N = (int)buf.size();
    const int nb_data_bytes = 2;
    if (N != nb_data_bytes) {
        LOG_ERROR("fig 0/7 Length doesn't match expectations ({}/{})",
            N, nb_data_bytes);
        return;
    }

    const uint8_t nb_services = (buf[0] & 0b11111100) >> 2;
    const uint16_t reconfiguration_count = 
          (static_cast<uint16_t>(buf[0] & 0b00000011) << 8) |
                               ((buf[1] & 0b11111111) >> 0);
    
    LOG_MESSAGE("fig 0/7 total_services={} reconfiguration_count={}",
        nb_services, reconfiguration_count);
    
    m_handler->OnConfigurationInformation_1(
        nb_services, reconfiguration_count);
}

// Service component global definition 
void FIG_Processor::ProcessFIG_Type_0_Ext_8(const FIG_Header_Type_0 header, tcb::span<const uint8_t> buf) {
    const int N = (int)buf.size();
    const int nb_service_id_bytes = header.pd ? 4 : 2;
    // In addition to the service id field, we have an additional byte of fields
    const int nb_header_bytes = nb_service_id_bytes+1;

    int curr_index = 0;
    int curr_service = 0;
    while (curr_index < N) {
        // Get the service data
        auto* service_buf = &buf[curr_index];
        const int nb_remain_bytes = N-curr_index;

        if ((nb_header_bytes+1) > nb_remain_bytes) {
            LOG_ERROR("fig 0/8 Message not long enough for header field ({})",
                nb_remain_bytes);
            return;
        }

        ServiceIdentifier sid;
        if (!header.pd) {
            sid.ProcessShortForm({service_buf, (size_t)nb_service_id_bytes});
        } else {
            sid.ProcessLongForm({service_buf, (size_t)nb_service_id_bytes});
        }

        const uint8_t descriptor = service_buf[nb_service_id_bytes];
        const uint8_t ext_flag = (descriptor & 0b10000000) >> 7;
        const uint8_t rfa0     = (descriptor & 0b01110000) >> 4;
        const uint8_t SCIdS    = (descriptor & 0b00001111) >> 0;

        // short or long form
        auto* data_buf = &service_buf[nb_service_id_bytes+1];
        const uint8_t ls_flag = 
            (data_buf[0] & 0b10000000) >> 7;

        const int nb_data_bytes = ls_flag ? 2 : 1;
        // We have an 8bit rfa field at the end if ext_flag is defined
        const int nb_rfa_byte = ext_flag ? 1 : 0;

        const uint8_t nb_length_bytes = nb_header_bytes + nb_data_bytes + nb_rfa_byte;

        if (nb_length_bytes > nb_remain_bytes) {
            LOG_ERROR("fig 0/8 Message not long enough for tail data ({}/{})", 
                nb_length_bytes, nb_remain_bytes);
            return;
        }

        const uint8_t rfa2 = ext_flag ? data_buf[nb_data_bytes] : 0x00; 

        if (!ls_flag) {
            const uint8_t rfu0          = (data_buf[0] & 0b01000000) >> 6;
            const uint8_t subchannel_id = (data_buf[0] & 0b00111111) >> 0;
            LOG_MESSAGE("fig 0/8 pd={} country_id={:>2} service_ref={:>4} ecc={} ext={} rfa0={} SCIdS={} is_long={} rfu0={} subchannel_id={:>2} rfa2={}",
                header.pd, 
                sid.country_id, sid.service_reference, sid.ecc, 
                ext_flag, rfa0, SCIdS,
                ls_flag, rfu0, subchannel_id, rfa2);
            
            m_handler->OnServiceComponent_4_Short_Definition(
                sid.country_id, sid.service_reference, sid.ecc,
                SCIdS, subchannel_id);
        } else {
            const uint8_t rfa1 =          (data_buf[0] & 0b01110000) >> 4;
            const uint16_t SCId = 
                    (static_cast<uint16_t>(data_buf[0] & 0b00001111) << 8) |
                                         ((data_buf[1] & 0b11111111) >> 0);
            LOG_MESSAGE("fig 0/8 pd={} country_id={:>2} service_ref={:>4} ecc={} ext={} rfa0={} SCIdS={} is_long={} rfa1={} SCId={:>2} rfa2={}",
                header.pd, 
                sid.country_id, sid.service_reference, sid.ecc, 
                ext_flag, rfa0, SCIdS,
                ls_flag, rfa1, SCId, rfa2);
            
            m_handler->OnServiceComponent_4_Long_Definition(
                sid.country_id, sid.service_reference, sid.ecc,
                SCIdS, SCId);
        }

        // Move to the next service
        curr_index += nb_length_bytes;
        curr_service++;
    }

    (void)curr_service;
}

// Country, LTO and International Table
void FIG_Processor::ProcessFIG_Type_0_Ext_9(const FIG_Header_Type_0 header, tcb::span<const uint8_t> buf) {
    const int N = (int)buf.size();
    const int nb_header_bytes = 3;
    if (nb_header_bytes > N) {
        LOG_ERROR("fig 0/9 Insufficient length for header ({}/{})",
            nb_header_bytes, N);
        return;
    }

    const uint8_t ext_flag =       (buf[0] & 0b10000000) >> 7;
    const uint8_t Rfa1 =           (buf[0] & 0b01000000) >> 6;
    const uint8_t ensemble_lto =   (buf[0] & 0b00111111) >> 0;

    const uint8_t ensemble_ecc =   (buf[1] & 0b11111111) >> 0;
    const uint8_t inter_table_id = (buf[2] & 0b11111111) >> 0;

    // LTO = local time offset
    // b5 | b4 b3 b2 b1 b0
    // LTO in hours = (-1)^b5 * (b4:b0) * 0.5
    
    // International table id selects which table to use for programme types
    // Refer to clause 5.7 International Table Identifiers in TS 101 756

    const int nb_ext_bytes = N-nb_header_bytes;

    if (ext_flag && (nb_ext_bytes <= 0)) {
        LOG_ERROR("fig 0/9 Insufficient length for extended field ({})",
            nb_ext_bytes);
        return;
    }

    if (!ext_flag && (nb_ext_bytes > 0)) {
        LOG_ERROR("fig 0/9 Extra bytes unaccounted for no extended fields ({})",
            nb_ext_bytes);
        return;
    }

    // no extended field
    if (!ext_flag) {
        LOG_MESSAGE("fig 0/9 ext={} Rfa1={} ensemble_lto={} ensemble_ecc={:02X} inter_table_id={}",
            ext_flag, Rfa1, ensemble_lto, ensemble_ecc, inter_table_id);
        
        m_handler->OnEnsemble_2_Country(
            ensemble_lto, ensemble_ecc, inter_table_id);
        return;
    }

    // subfields in extended field
    // each subfield contains a list of 16bit service ids
    const int nb_subfield_header_bytes = 2;
    const int nb_service_id_bytes = 2;

    auto* extended_buf = &buf[nb_header_bytes];
    int curr_byte = 0;
    int curr_subfield = 0;

    while (curr_byte < nb_ext_bytes) {
        const int nb_ext_remain_bytes = nb_ext_bytes-curr_byte;
        if (nb_subfield_header_bytes > nb_ext_remain_bytes) {
            LOG_ERROR("fig 0/9 Insufficient length for subfield header ({}/{})",
                nb_subfield_header_bytes, nb_ext_remain_bytes);
            return;
        }

        auto* subfield_buf = &extended_buf[curr_byte];
        const uint8_t nb_services = (subfield_buf[0] & 0b11000000) >> 6;
        const uint8_t Rfa2 =        (subfield_buf[0] & 0b00111111) >> 0;
        const uint8_t service_ecc = (subfield_buf[1] & 0b11111111) >> 0;

        const int nb_remain_list_bytes = nb_ext_remain_bytes-nb_subfield_header_bytes;
        const int nb_list_bytes = nb_services*nb_service_id_bytes;

        if (nb_list_bytes > nb_remain_list_bytes) {
            LOG_ERROR("fig 0/9 Insufficient length for service id list ({}/{})",
                nb_list_bytes, nb_remain_list_bytes);
            return;
        }

        auto* service_ids_buf = &subfield_buf[nb_subfield_header_bytes];
        for (int i = 0; i < nb_services; i++) {
            auto* b = &service_ids_buf[i*nb_service_id_bytes];
            ServiceIdentifier sid;
            sid.ProcessShortForm({b, (size_t)2});
            sid.ecc = service_ecc;
            LOG_MESSAGE("fig 0/9 ext={} Rfa1={} ensemble_lto={} ensemble_ecc={} inter_table_id={} Rfa2={} ECC={} i={}-{}/{} service_country_id={} service_ref={} service_ecc={}",
                ext_flag, Rfa1, ensemble_lto, ensemble_ecc, inter_table_id,
                Rfa2, service_ecc, 
                curr_subfield, i, nb_services,
                sid.country_id, sid.service_reference, sid.ecc);
            
            m_handler->OnEnsemble_2_Service_Country(
                ensemble_lto, ensemble_ecc, inter_table_id,
                sid.country_id, sid.service_reference, sid.ecc);
        }

        curr_subfield++;
        curr_byte += (nb_subfield_header_bytes + nb_list_bytes);
    }
}

// Date and time
void FIG_Processor::ProcessFIG_Type_0_Ext_10(const FIG_Header_Type_0 header, tcb::span<const uint8_t> buf) {
    const int N = (int)buf.size();
    const int nb_min_bytes = 4;
    if (nb_min_bytes > N) {
        LOG_ERROR("fig 0/10 Insufficient length for minimum configuration ({}/{})",
            nb_min_bytes, N);
        return;
    }

    const uint8_t rfu0 =      (buf[0] & 0b10000000) >> 7;
    const uint32_t MJD =
        (static_cast<uint32_t>(buf[0] & 0b01111111) << 10) |
        (static_cast<uint32_t>(buf[1] & 0b11111111) << 2 ) |
                             ((buf[2] & 0b11000000) >> 6 );
    const uint8_t LSI =       (buf[2] & 0b00100000) >> 5;
    const uint8_t Rfa0 =      (buf[2] & 0b00010000) >> 4;
    const uint8_t UTC =       (buf[2] & 0b00001000) >> 3;

    const int nb_actual_bytes = UTC ? 6 : 4;
    if (nb_actual_bytes > N) {
        LOG_ERROR("fig 0/10 Insufficient length for long form UTC ({}/{})",
            nb_actual_bytes, N);
        return;
    }


    const uint8_t hours =    ((buf[2] & 0b00000111) << 2) |
                             ((buf[3] & 0b11000000) >> 6);
    const uint8_t minutes =   (buf[3] & 0b00111111) >> 0;

    uint8_t seconds = 0;
    uint16_t milliseconds = 0;

    // long form utc has seconds and milliseconds
    if (UTC) {
        seconds =                 (buf[4] & 0b11111100) >> 2;
        milliseconds = 
            (static_cast<uint16_t>(buf[4] & 0b00000011) << 8) |
                                 ((buf[5] & 0b11111111) >> 0);
    }

    LOG_MESSAGE("fig 0/10 rfu0={} MJD={} LSI={} Rfa0={} UTC={} time={:02}:{:02}:{:02}.{:03}",
        rfu0, 
        MJD,
        LSI, Rfa0, UTC,
        hours, minutes, seconds, milliseconds);
    
    m_handler->OnDateTime_1(
        MJD, hours, minutes, seconds, milliseconds,
        LSI, UTC);
}

// User application information
void FIG_Processor::ProcessFIG_Type_0_Ext_13(const FIG_Header_Type_0 header, tcb::span<const uint8_t> buf) {
    const int N = (int)buf.size();
    const int nb_service_id_bytes = header.pd ? 4 : 2;
    // In addition to the service id field, we have an additional byte of fields
    const int nb_header_bytes = nb_service_id_bytes+1;

    int curr_byte = 0;
    int curr_block = 0;
    while (curr_byte != N) {
        const int nb_remain_bytes = N-curr_byte;
        if (nb_header_bytes > nb_remain_bytes) {
            LOG_ERROR("fig 0/13 Length not long enough for header data ({})", 
                nb_remain_bytes);
            return;
        }

        auto* entity_buf = &buf[curr_byte];

        ServiceIdentifier sid;
        if (!header.pd) {
            sid.ProcessShortForm({entity_buf, (size_t)nb_service_id_bytes});
        } else {
            sid.ProcessLongForm({entity_buf, (size_t)nb_service_id_bytes});
        }

        const uint8_t descriptor = entity_buf[nb_service_id_bytes];
        const uint8_t SCIdS        = (descriptor & 0b11110000) >> 4;
        const uint8_t nb_user_apps = (descriptor & 0b00001111) >> 0;

        auto* apps_buf = &entity_buf[nb_header_bytes];
        int curr_apps_buf_index = 0;
        const int nb_app_header_bytes = 2;

        // Go through all user apps in user app information block
        for (int i = 0; i < nb_user_apps; i++) {
            const int nb_app_remain_bytes = nb_remain_bytes-curr_apps_buf_index;
            auto* app_buf = &apps_buf[curr_apps_buf_index];

            if (nb_app_header_bytes > nb_app_remain_bytes) {
                LOG_ERROR("fig 0/13 Length not long enough for app header data ({}/{})",
                    nb_app_header_bytes, nb_app_remain_bytes);
                return;
            }

            const uint16_t user_app_type = 
                        (static_cast<uint16_t>(app_buf[0] & 0b11111111) << 3) |
                                             ((app_buf[1] & 0b11100000) >> 5);
            
            // Length of XPAD and user app data field
            const uint8_t nb_app_data_bytes = (app_buf[1] & 0b00011111) >> 0;

            const uint8_t nb_app_total_bytes = nb_app_header_bytes + nb_app_data_bytes;
            if (nb_app_total_bytes > nb_app_remain_bytes) {
                LOG_ERROR("fig 0/13 Length not long enough for app XPAD/user data ({}/{})",
                    nb_app_total_bytes, nb_app_remain_bytes);
                return;
            }

            LOG_MESSAGE("fig 0/13 pd={} country_id={:>2} service_ref={:>4} ecc={} SCIdS={} i={}-{}/{} app_type={} L={}",
                header.pd,
                sid.country_id, sid.service_reference, sid.ecc,
                SCIdS, 
                curr_block, i, nb_user_apps, 
                user_app_type, nb_app_data_bytes);

            auto* app_data_buf = (nb_app_data_bytes > 0) ? &app_buf[nb_app_header_bytes] : nullptr;
            m_handler->OnServiceComponent_5_UserApplication(
                sid.country_id, sid.service_reference, sid.ecc,
                SCIdS, 
                user_app_type, app_data_buf, nb_app_data_bytes);

            curr_apps_buf_index += nb_app_total_bytes;
        }

        // Move to next user app information block
        curr_byte += (curr_apps_buf_index + nb_header_bytes);
        curr_block++;
    }
}

// Subchannel for packet mode MSC FEC type
void FIG_Processor::ProcessFIG_Type_0_Ext_14(const FIG_Header_Type_0 header, tcb::span<const uint8_t> buf) {
    const int N = (int)buf.size();

    for (int i = 0; i < N; i++) {
        const uint8_t v = buf[i];
        const uint8_t subchannel_id = (v & 0b11111100) >> 2;
        const uint8_t fec           = (v & 0b00000011) >> 0;
        LOG_MESSAGE("fig 0/14 i={}/{} id={:>2} fec={}",
            i, N,
            subchannel_id, fec);
        
        m_handler->OnSubchannel_2_FEC(
            subchannel_id, fec);
    }
}

// Programme type
void FIG_Processor::ProcessFIG_Type_0_Ext_17(
    const FIG_Header_Type_0 header, 
    tcb::span<const uint8_t> buf)
{
    const int N = (int)buf.size();
    const int nb_min_bytes = 4;

    // NOTE: Referring to the welle.io code
    // This fig 0/17 has been expanded with additional parameters
    // This includes CC and language flags, which also changes the byte length to between 4 and 6

    // TODO: find the documentation that actually explicitly states this
    // The current document EN 300 401 v2.1.1 doesn't have this properly documented

    int curr_byte = 0;
    int curr_programme = 0;
    while (curr_byte < N) {
        auto* b = &buf[curr_byte];
        const int nb_remain_bytes = N-curr_byte;
        if (nb_remain_bytes < nb_min_bytes) {
            LOG_ERROR("fig 0/17 Remaining buffer doesn't have minimum bytes ({}/{})",
                nb_min_bytes, nb_remain_bytes);
            return;
        }

        ServiceIdentifier sid;
        sid.ProcessShortForm({b, (size_t)2});

        // NOTE: Fields according to ETSI EN 300 401
        // const uint8_t SD =    (b[2] & 0b10000000) >> 7;
        // const uint8_t Rfa1 =  (b[2] & 0b01000000) >> 6;
        // const uint8_t Rfu1 =  (b[2] & 0b00110000) >> 4;
        // const uint8_t Rfa2 = ((b[2] & 0b00001111) << 2) |
        //                      ((b[4] & 0b11000000) >> 6);
        // const uint8_t Rfu2 =  (b[4] & 0b00100000) >> 5;
        // const uint8_t international_code = 
        //                       (b[4] & 0b00011111) >> 0;

        // NOTE: Fields according to 
        // Source: https://github.com/AlbrechtL/welle.io
        // Reference: src/backend/fib-processor.cpp 
        const uint8_t SD =            (b[2] & 0b10000000) >> 7;
        const uint8_t language_flag = (b[2] & 0b00100000) >> 5;
        const uint8_t cc_flag =       (b[2] & 0b00010000) >> 4;

        uint8_t language_type = 0;
        uint8_t cc_type = 0;

        const int nb_bytes = nb_min_bytes + language_flag + cc_flag;
        int data_index = 3;

        if (nb_remain_bytes < nb_bytes) {
            LOG_ERROR("fig 0/17 Insufficient bytes for langugage ({}) and caption ({}) field ({}/{})",
                language_flag, cc_flag,
                nb_bytes, nb_remain_bytes);
            return;
        }

        if (language_flag) {
            language_type = b[data_index++];
        }

        if (cc_flag) {
            cc_type = b[data_index++];
        }

        const uint8_t international_code = (b[data_index++] & 0b00011111) >> 0;
        
        // LOG_MESSAGE("fig 0/17 pd={} country_id={} service_ref={:>4} ecc={} i={}/{} SD={} Rfa1={} Rfu1={} Rfa2={} Rfu2={} inter_code={}",
        //     //     header.pd,
        //     sid.country_id, sid.service_reference, sid.ecc,
        //     i, nb_programmes,
        //     SD, Rfa1, Rfu1, Rfa2, Rfu2, 
        //     international_code);

        LOG_MESSAGE("fig 0/17 pd={} country_id={} service_ref={:>4} ecc={} i={} SD={} L_flag={} cc_flag={} inter_code={:>2} language={} CC={}",
            header.pd,
            sid.country_id, sid.service_reference, sid.ecc,
            curr_programme,
            SD, language_flag, cc_flag, 
            international_code,
            language_type, cc_type);
        
        m_handler->OnService_1_ProgrammeType(
            sid.country_id, sid.service_reference, sid.ecc, 
            international_code, language_type, cc_type,
            language_flag, cc_flag);

        curr_byte += nb_bytes; 
        curr_programme++;
    }
}

// Frequency information
void FIG_Processor::ProcessFIG_Type_0_Ext_21(const FIG_Header_Type_0 header, tcb::span<const uint8_t> buf) {
    const int N = (int)buf.size();
    const int nb_block_header_bytes = 2;

    // We have a list of blocks
    // Each block contains a list of frequency information lists
    // Each frequency information list contains different types of ids depending on RM field

    int curr_byte = 0;
    int curr_block = 0;
    while (curr_byte < N) {
        const int nb_remain_bytes = N-curr_byte;
        if (nb_block_header_bytes > nb_remain_bytes) {
            LOG_ERROR("fig 0/21 Insufficient length for block header ({}/{})",
                nb_block_header_bytes, nb_remain_bytes);
            return;
        }

        // increment this to update the block size
        auto* block_buf = &buf[curr_byte];

        const uint16_t Rfa0 = 
            (static_cast<uint16_t>(block_buf[0] & 0b11111111) << 3) |
                                 ((block_buf[1] & 0b11100000) >> 5);

        const uint8_t nb_fi_list_bytes = 
                                  (block_buf[1] & 0b00011111) >> 0;

        // loop through each frequency information list
        const int nb_fi_list_header_bytes = 3;

        auto* fi_lists_buf = &block_buf[nb_block_header_bytes];
        int curr_fi_byte = 0;
        int curr_fi_list = 0;
        while (curr_fi_byte < nb_fi_list_bytes) {
            const int nb_fi_remain_bytes = nb_fi_list_bytes-curr_fi_byte;
            if (nb_fi_list_header_bytes > nb_fi_remain_bytes) {
                LOG_ERROR("fig 0/21 Insufficient length for fi list header ({}/{})",
                    nb_fi_list_header_bytes, nb_fi_remain_bytes);
                return;
            }

            auto* fi_list_buf = &fi_lists_buf[curr_fi_byte];
            const uint16_t id = 
                         (static_cast<uint16_t>(fi_list_buf[0] & 0b11111111) << 8) | 
                                              ((fi_list_buf[1] & 0b11111111) >> 0);
            const uint8_t RM =                 (fi_list_buf[2] & 0b11110000) >> 4;
            const uint8_t continuity_flag =    (fi_list_buf[2] & 0b00001000) >> 3;
            const uint8_t nb_freq_list_bytes = (fi_list_buf[2] & 0b00000111) >> 0;

            // continuity flag is interpreted differently between different RM types

            auto* freq_list_buf = &fi_list_buf[3];
            switch (RM) {
            case 0b0000:
                {
                    // ID: Clause 6.4
                    EnsembleIdentifier eid;
                    eid.ProcessU16(id);

                    const bool is_continuous_output = continuity_flag;

                    const uint8_t nb_entry_bytes = 3;
                    if ((nb_freq_list_bytes % nb_entry_bytes) != 0) {
                        LOG_ERROR("fig 0/21 Frequency list RM={} doesn't have a list length that is a multiple ({}{})",
                            RM, nb_freq_list_bytes, nb_entry_bytes);
                        return;
                    }
                    const int nb_entries = nb_freq_list_bytes / nb_entry_bytes;
                    for (int i = 0; i < nb_entries; i++) {
                        auto* b = &freq_list_buf[i*nb_entry_bytes];
                        const uint8_t control_field = (b[0] & 0b11111000) >> 3;
                        const uint32_t freq = 
                                (static_cast<uint32_t>(b[0] & 0b00000111) << 16) |
                                (static_cast<uint32_t>(b[1] & 0b11111111) << 8 ) |
                                                     ((b[2] & 0b11111111) << 0 );

                        // F' = F*16kHz
                        const uint32_t alt_freq = freq*16000u;

                        // interpret the control field for alternate ensemble
                        const bool is_geographically_adjacent = !(control_field & 0b1);
                        const bool is_transmission_mode_I = (control_field & 0b10);

                        LOG_MESSAGE("fig 0/21 i={}-{}-{}/{} Rfa0={} RM={} is_continuous={} country_id={} ensemble_ref={} is_adjacent={} is_mode_I={} freq={}",
                            curr_block, curr_fi_list, i, nb_entries, 
                            Rfa0, RM, 
                            is_continuous_output, 
                            eid.country_id, eid.ensemble_reference,
                            is_geographically_adjacent, is_transmission_mode_I,
                            (float)(alt_freq)*1e-6f);
                        m_handler->OnFrequencyInformation_1_Ensemble(
                            eid.country_id, eid.ensemble_reference, 
                            alt_freq, 
                            is_continuous_output,
                            is_geographically_adjacent,
                            is_transmission_mode_I);
                    }
                }
                break;
            case 0b1000:
                {
                    // ID: RDS PI-code (see IEC 62106 [10]) for FM radio
                    const bool is_time_compensated = continuity_flag;
                    const uint16_t rds_pi_code = id;

                    for (int i = 0; i < nb_freq_list_bytes; i++) {
                        const uint8_t freq = freq_list_buf[i];
                        // alternative frequency on an AM or FM station
                        // F' = 87.5MHz + F*100kHz
                        const uint32_t alt_freq = 87500000u + freq*100000u;
                        LOG_MESSAGE("fig 0/21 i={}-{}-{}/{} Rfa0={} RM={} time_compensated={} RDS_PI={:04X} freq={}",
                            curr_block, curr_fi_list, i, nb_freq_list_bytes, 
                            Rfa0, RM, is_time_compensated, 
                            rds_pi_code,
                            (float)(alt_freq)*1e-6f);
                        m_handler->OnFrequencyInformation_1_RDS_PI(
                            rds_pi_code, alt_freq, is_time_compensated);
                    }
                }
                break;
            case 0b0110:
                {
                    // ID: DRM Service Identifier (two least significant bytes) 
                    // ETSI ES 201 980 [8]
                    const bool is_time_compensated = continuity_flag;

                    const uint8_t nb_entry_bytes = 3;
                    if ((nb_freq_list_bytes % nb_entry_bytes) != 0) {
                        LOG_ERROR("fig 0/21 Frequency list RM={} doesn't have a list length that is a multiple ({}{})",
                            RM, nb_freq_list_bytes, nb_entry_bytes);
                        return;
                    }
                    const int nb_entries = nb_freq_list_bytes / nb_entry_bytes;
                    for (int i = 0; i < nb_entries; i++) {
                        auto* b = &freq_list_buf[i*nb_entry_bytes];
                        const uint8_t drm_id_msb =    (b[0] & 0b11111111) >> 0;

                        const uint8_t is_multiplier = (b[1] & 0b10000000) >> 7;
                        const uint16_t freq = 
                                (static_cast<uint16_t>(b[1] & 0b01111111) << 8) | 
                                                     ((b[2] & 0b11111111) << 0);
                        const uint32_t drm_id = (static_cast<uint32_t>(drm_id_msb) << 16) | id;
                        // F' = k*F
                        // k = 1kHz or 10kHz depending on the multiplier flag
                        const uint32_t multiplier = is_multiplier ? 10000u : 1000u;
                        const uint32_t alt_freq = multiplier*freq;

                        LOG_MESSAGE("fig 0/21 i={}-{}-{}/{} Rfa0={} RM={} time_compensated={} DRM_id={} freq={}",
                            curr_block, curr_fi_list, i, nb_entries, 
                            Rfa0, RM, is_time_compensated, 
                            drm_id, (float)(alt_freq)*1e-6f);
                        m_handler->OnFrequencyInformation_1_DRM(
                            drm_id, alt_freq, is_time_compensated);
                    }
                }
                break;
            case 0b1110:
                {
                    // ID: AMSS Service Identifier (most significant byte) 
                    // ETSI TS 102 386
                    const bool is_time_compensated = continuity_flag;

                    const uint8_t nb_entry_bytes = 3;
                    if ((nb_freq_list_bytes % nb_entry_bytes) != 0) {
                        LOG_ERROR("fig 0/21 Frequency list RM={} doesn't have a list length that is a multiple ({}{})",
                            RM, nb_freq_list_bytes, nb_entry_bytes);
                        return;
                    }
                    const int nb_entries = nb_freq_list_bytes / nb_entry_bytes;
                    for (int i = 0; i < nb_entries; i++) {
                        auto* b = &freq_list_buf[i*nb_entry_bytes];
                        const uint8_t amss_id_msb = (b[0] & 0b11111111) >> 0;
                        const uint16_t freq = 
                              (static_cast<uint16_t>(b[1] & 0b11111111) << 8) | 
                                                   ((b[2] & 0b11111111) << 0);
                        const uint32_t amss_id = (static_cast<uint32_t>(amss_id_msb) << 16) | id;

                        // F' = F*1kHz 
                        const uint32_t alt_freq = freq*1000u;

                        LOG_MESSAGE("fig 0/21 i={}-{}-{}/{} Rfa0={} RM={} time_compensated={} AMSS_id={} freq={}",
                            curr_block, curr_fi_list, i, nb_entries, 
                            Rfa0, RM, is_time_compensated, 
                            amss_id, (float)(alt_freq)*1e-6f);
                        m_handler->OnFrequencyInformation_1_AMSS(
                            amss_id, alt_freq, is_time_compensated);
                    }
                }
                break;
            default:
                LOG_ERROR("fig 0/21 Unknown RM value ({})", RM);
                return;
            }

            curr_fi_byte += (nb_fi_list_header_bytes + nb_freq_list_bytes);
            curr_fi_list++;
        }

        curr_byte += (nb_block_header_bytes + nb_fi_list_bytes);
        curr_block++;
    }
}

// OE Services for service following?
void FIG_Processor::ProcessFIG_Type_0_Ext_24(const FIG_Header_Type_0 header, tcb::span<const uint8_t> buf) {
    const int N = (int)buf.size();
    const int nb_sid_bytes = header.pd ? 4 : 2;
    const int nb_header_bytes = nb_sid_bytes + 1;

    int curr_byte = 0;
    while (curr_byte < N) {
        const int nb_remain_bytes = N-curr_byte;
        if (nb_header_bytes > nb_remain_bytes) {
            LOG_ERROR("fig 0/24 Insufficient length for header bytes ({}/{})",
                nb_header_bytes, nb_remain_bytes);
            return;
        }

        auto* b = &buf[curr_byte];

        ServiceIdentifier sid;
        if (!header.pd) {
            sid.ProcessShortForm({b, (size_t)nb_sid_bytes});
        } else {
            sid.ProcessLongForm({b, (size_t)nb_sid_bytes});
        }

        const uint8_t descriptor = b[nb_sid_bytes];
        const uint8_t Rfa =     (descriptor & 0b10000000) >> 7;
        const uint8_t CAId =    (descriptor & 0b01110000) >> 4;
        const uint8_t nb_EIds = (descriptor & 0b00001111) >> 0;

        const int nb_EId_bytes = 2;
        const int nb_EId_list_bytes = nb_EId_bytes*nb_EIds;
        const int nb_EId_list_remain_bytes = nb_remain_bytes - nb_header_bytes;

        if (nb_EId_list_bytes > nb_EId_list_remain_bytes) {
            LOG_ERROR("fig 0/24 Insufficient length for EId list ({}/{})",
                nb_EId_list_bytes, nb_EId_list_remain_bytes);
            return;
        }

        auto* eids_buf = &b[nb_header_bytes];
        for (int i = 0; i < nb_EIds; i++) {
            auto* eid_buf = &eids_buf[i*nb_EId_bytes];
            EnsembleIdentifier eid;
            eid.ProcessBuffer({eid_buf, (size_t)nb_EId_bytes});

            LOG_MESSAGE("fig 0/24 country_id={} service_ref={} ecc={} Rfa={} CAId={} i={}/{} ensemble_country_id={} ensemble_reference={}",
                sid.country_id, sid.service_reference, sid.ecc,
                Rfa, CAId, i, nb_EIds,
                eid.country_id, eid.ensemble_reference);
            
            m_handler->OnOtherEnsemble_1_Service(
                sid.country_id, sid.service_reference, sid.ecc,
                eid.country_id, eid.ensemble_reference);
        }
        curr_byte += (nb_header_bytes + nb_EId_list_bytes);
    }
}

// Ensemble label
void FIG_Processor::ProcessFIG_Type_1_Ext_0(const FIG_Header_Type_1 header, tcb::span<const uint8_t> buf) {
    const int N = (int)buf.size();
    const int nb_eid_bytes = 2;
    const int nb_char_bytes = 16;
    const int nb_flag_bytes = 2;
    const int nb_expected_bytes = nb_eid_bytes + nb_char_bytes + nb_flag_bytes;

    if (N != nb_expected_bytes) {
        LOG_ERROR("fig 1/0 Expected {} bytes got {} bytes",
            nb_expected_bytes, N);
        return;
    }

    EnsembleIdentifier eid;
    eid.ProcessBuffer({buf.data(), (size_t)nb_eid_bytes});

    auto* char_buf = &buf[nb_eid_bytes];
    // flag field is used for determining which characters can be removed
    // when we are abbreviating the label
    const int flag_index = nb_eid_bytes + nb_char_bytes;
    const uint16_t flag_field = 
        (static_cast<uint16_t>(buf[flag_index+0]) << 8) | 
                               buf[flag_index+1];
    
    LOG_MESSAGE("fig 1/0 charset={} country_id={} ensemble_ref={:>4} flag={:04X} chars={}",
        header.charset,
        eid.country_id, eid.ensemble_reference,
        flag_field,
        std::string_view{reinterpret_cast<const char*>(char_buf), nb_char_bytes});
    
    m_handler->OnEnsemble_3_Label(
        eid.country_id, eid.ensemble_reference,
        flag_field, {char_buf, (size_t)nb_char_bytes});
}

// Short form service identifier label
void FIG_Processor::ProcessFIG_Type_1_Ext_1(const FIG_Header_Type_1 header, tcb::span<const uint8_t> buf) {
    const int N = (int)buf.size();
    const int nb_sid_bytes = 2;
    const int nb_char_bytes = 16;
    const int nb_flag_bytes = 2;
    const int nb_expected_bytes = nb_sid_bytes + nb_char_bytes + nb_flag_bytes;

    if (N != nb_expected_bytes) {
        LOG_ERROR("fig 1/1 Expected {} bytes got {} bytes",
            nb_expected_bytes, N);
        return;
    }

    ServiceIdentifier sid;
    sid.ProcessShortForm(buf);

    auto* char_buf = &buf[nb_sid_bytes];
    const int flag_index = nb_sid_bytes + nb_char_bytes;
    const uint16_t flag_field = 
        (static_cast<uint16_t>(buf[flag_index+0]) << 8) | 
                               buf[flag_index+1];
    
    LOG_MESSAGE("fig 1/1 charset={} country_id={} service_ref={:>4} ecc={} flag={:04X} chars={}",
        header.charset,
        sid.country_id, sid.service_reference, sid.ecc,
        flag_field,
        std::string_view{reinterpret_cast<const char*>(char_buf), nb_char_bytes});

    m_handler->OnService_2_Label(
        sid.country_id, sid.service_reference, sid.ecc,
        flag_field, {char_buf, (size_t)nb_char_bytes});
}

// Service component label (non primary)
void FIG_Processor::ProcessFIG_Type_1_Ext_4(const FIG_Header_Type_1 header, tcb::span<const uint8_t> buf) {
    const int N = (int)buf.size();
    const int nb_header_bytes = 1;
    const int nb_char_bytes = 16;
    const int nb_flag_bytes = 2;

    if (N < nb_header_bytes) {
        LOG_ERROR("fig 1/4 Expected at least {} byte for header got {} bytes",
            nb_header_bytes, N);
        return;
    }

    const uint8_t descriptor = buf[0];
    const uint8_t pd =    (descriptor & 0b10000000) >> 7;
    // const uint8_t Rfa =   (descriptor & 0b01110000) >> 4;
    const uint8_t SCIdS = (descriptor & 0b00001111) >> 0;

    const int nb_sid_bytes = pd ? 4 : 2;
    const int nb_expected_bytes = nb_header_bytes + nb_sid_bytes + nb_char_bytes + nb_flag_bytes;

    if (N != nb_expected_bytes) {
        LOG_ERROR("fig 1/4 Expected {} bytes got {} bytes",
            nb_expected_bytes, N);
        return;
    }

    ServiceIdentifier sid;
    if (!pd) {
        sid.ProcessShortForm({&buf[nb_header_bytes], (size_t)nb_sid_bytes});
    } else {
        sid.ProcessLongForm({&buf[nb_header_bytes], (size_t)nb_sid_bytes});
    }

    // iterated backwards
    auto* char_buf = &buf[nb_header_bytes+nb_sid_bytes];

    const int flag_index = nb_header_bytes + nb_sid_bytes + nb_char_bytes;
    const uint16_t flag_field = 
        (static_cast<uint16_t>(buf[flag_index+0]) << 8) | 
                               buf[flag_index+1];
    
    LOG_MESSAGE("fig 1/5 charset={} SCIdS={} country_id={} service_ref={:>4} ecc={} flag={:04X} chars={}",
        header.charset,
        SCIdS,
        sid.country_id, sid.service_reference, sid.ecc,
        flag_field,
        std::string_view{reinterpret_cast<const char*>(char_buf), nb_char_bytes});

    m_handler->OnServiceComponent_6_Label(
        sid.country_id, sid.service_reference, sid.ecc,
        SCIdS, 
        flag_field, {char_buf, (size_t)nb_char_bytes});
}

// Long form service identifier label
void FIG_Processor::ProcessFIG_Type_1_Ext_5(const FIG_Header_Type_1 header, tcb::span<const uint8_t> buf) {
    const int N = (int)buf.size();
    const int nb_sid_bytes = 4;
    const int nb_char_bytes = 16;
    const int nb_flag_bytes = 2;

    const int nb_expected_bytes = nb_sid_bytes + nb_char_bytes + nb_flag_bytes;
    if (N != nb_expected_bytes) {
        LOG_ERROR("fig 1/5 Expected {} bytes got {} bytes",
            nb_expected_bytes, N);
        return;
    }

    ServiceIdentifier sid;
    sid.ProcessLongForm(buf);

    auto* char_buf = &buf[nb_sid_bytes];
    const int flag_index = nb_sid_bytes + nb_char_bytes;
    const uint16_t flag_field = 
        (static_cast<uint16_t>(buf[flag_index+0]) << 8) | 
                               buf[flag_index+1];
    
    LOG_MESSAGE("fig 1/5 charset={} country_id={} service_ref={:>4} ecc={} flag={:04X} chars={}",
        header.charset,
        sid.country_id, sid.service_reference, sid.ecc,
        flag_field,
        std::string_view{reinterpret_cast<const char*>(char_buf), nb_char_bytes});
    
    m_handler->OnService_2_Label(
        sid.country_id, sid.service_reference, sid.ecc,
        flag_field, {char_buf, (size_t)nb_char_bytes});
}
