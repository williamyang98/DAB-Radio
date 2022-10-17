#include "fig_processor.h"
#include "fig_handler_interface.h"

#include <stdio.h>
#include <stdlib.h>

#define PRINT_LOG_MESSAGE 1
#define PRINT_LOG_ERROR 1

#if PRINT_LOG_MESSAGE
    #define LOG_MESSAGE(fmt, ...)    fprintf(stderr, "[fp] " fmt, ##__VA_ARGS__)
#else
    #define LOG_MESSAGE(...) (void)0
#endif

#if PRINT_LOG_ERROR
    #define LOG_ERROR(fmt, ...) fprintf(stderr, "ERROR: [fp] " fmt, ##__VA_ARGS__)
#else
    #define LOG_ERROR(...)   (void)0
#endif

struct ServiceIdentifier {
    uint8_t country_id = 0;
    uint32_t service_reference = 0;
    uint8_t ecc = 0;
    // 2 byte form
    void ProcessShortForm(const uint8_t* b) {
        country_id        =       (b[0] & 0b11110000) >> 4;
        service_reference =
            (static_cast<uint16_t>(b[0] & 0b00001111) << 8) |
                                 ((b[1] & 0b11111111) >> 0);
        ecc = 0;
    }
    // 4 byte form
    void ProcessLongForm(const uint8_t* b) {
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

    void ProcessBuffer(const uint8_t* buf) {
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

void FIG_Processor::ProcessFIG(const uint8_t* buf, const int cif_index) {
    // Dont do anything if we don't have an associated handler
    if (handler == NULL) {
        return;
    }

    const int N = 30;

    int curr_byte = 0;
    int curr_fig = 0;
    while (curr_byte < N) {
        const int nb_remain_bytes = N-curr_byte;

        const uint8_t header = buf[curr_byte];
        // delimiter byte
        if (header == 0xFF) {
            return;
        }

        const uint8_t fig_type =              (header & 0b11100000) >> 5;
        const uint8_t fig_data_length_bytes = (header & 0b00011111) >> 0;

        const uint8_t fig_length_bytes = fig_data_length_bytes+1;

        if (fig_length_bytes > nb_remain_bytes) {
            LOG_ERROR("[%d] fig specified length overflows buffer (%d/%d)\n",
                cif_index, fig_length_bytes, nb_remain_bytes);
            return;
        }

        const uint8_t* fig_buf = &buf[curr_byte+1];
        curr_byte += fig_length_bytes;

        // LOG_MESSAGE("[%d] FIG type start (%d)\n", cif_index, curr_fig++);

        switch (fig_type) {
        // MCI and part of SI
        case 0:
            ProcessFIG_Type_0(fig_buf, fig_data_length_bytes, cif_index);
            break;
        // Labels etc. part of SI
        case 1:
            ProcessFIG_Type_1(fig_buf, fig_data_length_bytes, cif_index);
            break;
        // Labels etc. part of SI
        case 2:
            ProcessFIG_Type_2(fig_buf, fig_data_length_bytes, cif_index);
            break;
        // Conditional access
        case 6:
            ProcessFIG_Type_6(fig_buf, fig_data_length_bytes, cif_index);
            break;
        // Ending byte of the FIG packet
        // If data occupying all 30 bytes, no delimiter present
        // If data occupying less than 30 bytes, delimiter present and any 0x00 padding afterwards
        case 7:
            curr_byte = N;
            return;
        // reserved 
        case 3:
        case 4:
        case 5:
        default:
            LOG_ERROR("Invalid fig type (%d)\n", fig_type);
            return;
        }
    }
}

void FIG_Processor::ProcessFIG_Type_0(const uint8_t* buf, const uint8_t N, const int cif_index) {
    const uint8_t descriptor = buf[0];

    FIG_Header_Type_0 header;

    header.cn               = (descriptor & 0b10000000) >> 7;
    header.oe               = (descriptor & 0b01000000) >> 6;
    header.pd               = (descriptor & 0b00100000) >> 5;
    const uint8_t extension = (descriptor & 0b00011111) >> 0;

    const uint8_t* field_buf = &buf[1];
    const uint8_t nb_field_bytes = N-1;

    // LOG_MESSAGE("fig 0/%d L=%d\n", extension, nb_field_bytes);

    switch (extension) {
    case 0 : ProcessFIG_Type_0_Ext_0 (header, field_buf, nb_field_bytes, cif_index); break;
    case 1 : ProcessFIG_Type_0_Ext_1 (header, field_buf, nb_field_bytes, cif_index); break;
    case 2 : ProcessFIG_Type_0_Ext_2 (header, field_buf, nb_field_bytes, cif_index); break;
    case 3 : ProcessFIG_Type_0_Ext_3 (header, field_buf, nb_field_bytes, cif_index); break;
    case 4 : ProcessFIG_Type_0_Ext_4 (header, field_buf, nb_field_bytes, cif_index); break;
    case 5 : ProcessFIG_Type_0_Ext_5 (header, field_buf, nb_field_bytes, cif_index); break;
    case 6 : ProcessFIG_Type_0_Ext_6 (header, field_buf, nb_field_bytes, cif_index); break;
    case 7 : ProcessFIG_Type_0_Ext_7 (header, field_buf, nb_field_bytes, cif_index); break;
    case 8 : ProcessFIG_Type_0_Ext_8 (header, field_buf, nb_field_bytes, cif_index); break;
    case 9 : ProcessFIG_Type_0_Ext_9 (header, field_buf, nb_field_bytes, cif_index); break;
    case 10: ProcessFIG_Type_0_Ext_10(header, field_buf, nb_field_bytes, cif_index); break;
    case 13: ProcessFIG_Type_0_Ext_13(header, field_buf, nb_field_bytes, cif_index); break;
    case 14: ProcessFIG_Type_0_Ext_14(header, field_buf, nb_field_bytes, cif_index); break;
    case 17: ProcessFIG_Type_0_Ext_17(header, field_buf, nb_field_bytes, cif_index); break;
    case 21: ProcessFIG_Type_0_Ext_21(header, field_buf, nb_field_bytes, cif_index); break;
    case 24: ProcessFIG_Type_0_Ext_24(header, field_buf, nb_field_bytes, cif_index); break;
    default:
        LOG_MESSAGE("[%d] fig 0/%u Unsupported\n", 
            cif_index, extension);
        break;
    }
}

void FIG_Processor::ProcessFIG_Type_1(const uint8_t* buf, const uint8_t N, const int cif_index) {
    const uint8_t descriptor = buf[0];

    FIG_Header_Type_1 header;
    header.charset          = (descriptor & 0b11110000) >> 4;
    header.rfu              = (descriptor & 0b00001000) >> 3;
    const uint8_t extension = (descriptor & 0b00000111) >> 0;

    const uint8_t* field_buf = &buf[1];
    const uint8_t nb_field_bytes = N-1;

    switch (extension) {
    case 0: ProcessFIG_Type_1_Ext_0(header, field_buf, nb_field_bytes, cif_index); break;
    case 1: ProcessFIG_Type_1_Ext_1(header, field_buf, nb_field_bytes, cif_index); break;
    case 4: ProcessFIG_Type_1_Ext_4(header, field_buf, nb_field_bytes, cif_index); break;
    case 5: ProcessFIG_Type_1_Ext_5(header, field_buf, nb_field_bytes, cif_index); break;
    default:
        LOG_MESSAGE("[%d] fig 1/%u L=%u Unsupported\n", cif_index, extension, nb_field_bytes);
        break;
    }
}

void FIG_Processor::ProcessFIG_Type_2(const uint8_t* buf, const uint8_t N, const int cif_index)
{
    const uint8_t descriptor = buf[0];

    FIG_Header_Type_2 header;
    header.toggle_flag      = (descriptor & 0b10000000) >> 7;
    header.segment_index    = (descriptor & 0b01110000) >> 4;
    header.rfu              = (descriptor & 0b00001000) >> 3;
    const uint8_t extension = (descriptor & 0b00000111) >> 0;

    const uint8_t* field_buf = &buf[1];
    const uint8_t nb_field_bytes = N-1;

    LOG_MESSAGE("[%d] fig 2/%u L=%u Unsupported\n", cif_index, extension, nb_field_bytes);
}

void FIG_Processor::ProcessFIG_Type_6(const uint8_t* buf, const uint8_t N, const int cif_index)
{
    const uint8_t descriptor = buf[0];

    const uint8_t rfu               = (descriptor & 0b10000000) >> 7;
    const uint8_t cn                = (descriptor & 0b01000000) >> 6;
    const uint8_t oe                = (descriptor & 0b00100000) >> 5;
    const uint8_t pd                = (descriptor & 0b00010000) >> 4;
    const uint8_t lef               = (descriptor & 0b00001000) >> 3;
    const uint8_t short_CA_sys_id   = (descriptor & 0b00000111) >> 0;

    LOG_MESSAGE("fig 6 L=%d Unsupported\n", N);
}

// Ensemble information
void FIG_Processor::ProcessFIG_Type_0_Ext_0(
    const FIG_Header_Type_0 header, 
    const uint8_t* buf, const uint8_t N, const int cif_index)
{
    const int nb_field_bytes = 4;
    if (N != nb_field_bytes) {
        LOG_ERROR("[%d] fig 0/0 Length doesn't match expectations (%d/%d)\n",
            cif_index, nb_field_bytes, N);
        return;
    }

    EnsembleIdentifier eid;
    eid.ProcessBuffer(buf);
    
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
    const uint8_t occurance_change = 0x00;

    LOG_MESSAGE("[%d] fig 0/0 country_id=%u ensemble_ref=%u change=%u alarm=%u cif=%u|%u\n",
        cif_index,
        eid.country_id, eid.ensemble_reference,
        change_flags, alarm_flag,
        cif_upper, cif_lower);
    
    handler->OnEnsemble_1_ID(
        cif_index, 
        eid.country_id, eid.ensemble_reference,
        change_flags, alarm_flag, 
        cif_upper, cif_lower);
}

// Subchannel for stream mode MSC
void FIG_Processor::ProcessFIG_Type_0_Ext_1(
    const FIG_Header_Type_0 header, 
    const uint8_t* buf, const uint8_t N, const int cif_index) 
{
    int curr_byte = 0;
    int curr_subchannel = 0;
    while (curr_byte < N) {
        auto* data = &buf[curr_byte];
        const uint8_t nb_remain = N-curr_byte;

        // Minimum length of header
        if (nb_remain < 3) {
            LOG_ERROR("[%d] fig 0/1 Ended early for some reason (%d)\n", cif_index, curr_byte);
            break;
        }
        
        const uint8_t subchannel_id = (data[0] & 0b11111100) >> 2;
        const uint16_t start_address =
                (static_cast<uint16_t>(data[0] & 0b00000011) << 8) |
                                     ((data[1] & 0b11111111) >> 0);

        const uint8_t is_long_form = (data[2] & 0b10000000) >> 7;
        const uint8_t nb_data_bytes = is_long_form ? 4 : 3;
        if (nb_data_bytes > nb_remain) {
            LOG_ERROR("[%d] fig 0/1 Long field cannot fit in remaining length\n", cif_index);
            break;
        }

        // process short form
        // this provides configuration on Unequal Error Protection
        if (!is_long_form) {
            const uint8_t table_switch = (data[2] & 0b01000000) >> 6;
            const uint8_t table_index  = (data[2] & 0b00111111) >> 0;

            LOG_MESSAGE("[%d] fig 0/1 i=%d subchannel_id=%-2u start_addr=%-3u long=%u table_switch=%u table_index=%u\n",
                cif_index,
                curr_subchannel,
                subchannel_id, start_address, is_long_form,
                table_switch, table_index);

            handler->OnSubchannel_1_Short(
                cif_index,
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

            LOG_MESSAGE("[%d] fig 0/1 i=%d subchannel_id=%-2u start_addr=%-3u long=%u option=%u prot_level=%u subchannel_size=%u\n",
                cif_index,
                curr_subchannel,
                subchannel_id, start_address, is_long_form,
                option, prot_level, subchannel_size);

            handler->OnSubchannel_1_Long(
                cif_index,
                subchannel_id, start_address,
                option, prot_level, subchannel_size);
        }
        curr_byte += nb_data_bytes;
        curr_subchannel++;
    }
}

// Service and service components information in stream mode
void FIG_Processor::ProcessFIG_Type_0_Ext_2(
    const FIG_Header_Type_0 header, 
    const uint8_t* buf, const uint8_t N, const int cif_index)
{
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
            LOG_ERROR("[%d] fig 0/2 Message not long enough header field for service data\n",
                cif_index);
            return;
        }

        ServiceIdentifier sid;
        if (!header.pd) {
            sid.ProcessShortForm(service_buf);
        } else {
            sid.ProcessLongForm(service_buf);
        }

        const uint8_t descriptor = service_buf[nb_service_id_bytes];
        const uint8_t rfa                   = (descriptor & 0b10000000) >> 7;
        const uint8_t CAId                  = (descriptor & 0b01110000) >> 4;
        const uint8_t nb_service_components = (descriptor & 0b00001111) >> 0;

        // Determine if we have enough bytes for the service components data
        const uint8_t nb_service_component_bytes = 2;
        const int nb_length_bytes = nb_service_component_bytes*nb_service_components + nb_header_bytes;

        if (nb_length_bytes > nb_remain_bytes) {
            LOG_ERROR("[%d] fig 0/2 Message not long enough for service components\n", cif_index);
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
                    LOG_MESSAGE("[%d] fig 0/2 pd=%u country_id=%-2u service_ref=%-4u ecc=%u i=%d-%d/%d tmid=%u ASTCy=%u subchannel_id=%-2u ps=%u ca=%u\n",
                        cif_index, header.pd,
                        sid.country_id, sid.service_reference, sid.ecc,
                        curr_service, i, nb_service_components,
                        tmid, 
                        ASTCy, subchannel_id, is_primary, ca_flag);
                    
                    handler->OnServiceComponent_1_StreamAudioType(
                        cif_index,
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
                    LOG_MESSAGE("[%d] fig 0/2 pd=%u country_id=%-2u service_ref=%-4u ecc=%u i=%d-%d/%d tmid=%u DSTCy=%u subchannel_id=%-2u ps=%u ca=%u\n",
                        cif_index, header.pd,
                        sid.country_id, sid.service_reference, sid.ecc,
                        curr_service, i, nb_service_components,
                        tmid, 
                        DSCTy, subchannel_id, is_primary, ca_flag);
                    
                    handler->OnServiceComponent_1_StreamDataType(
                        cif_index,
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
                    LOG_MESSAGE("[%d] fig 0/2 pd=%u country_id=%-2u service_ref=%-4u ecc=%u i=%d-%d/%d tmid=%u SCId=%u ps=%u ca=%u\n",
                        cif_index, header.pd,
                        sid.country_id, sid.service_reference, sid.ecc,
                        curr_service, i, nb_service_components,
                        tmid, 
                        SCId, is_primary, ca_flag);

                    handler->OnServiceComponent_1_PacketDataType(
                        cif_index,
                        sid.country_id, sid.service_reference, sid.ecc,
                        SCId, is_primary);
                }
                break;
            default:
                LOG_ERROR("[%d] fig 0/2 reserved tmid=%d\n", cif_index, tmid);
                return;
            }
        }

        // Move to the next service
        curr_index += nb_length_bytes;
        curr_service++;
    }
}

// Service components information in packet mode
void FIG_Processor::ProcessFIG_Type_0_Ext_3(
    const FIG_Header_Type_0 header, 
    const uint8_t* buf, const uint8_t N, const int cif_index)
{
    const int nb_header_bytes = 5;
    const int nb_CAOrg_field_bytes = 2;

    int curr_byte = 0;
    int curr_component = 0;
    while (curr_byte < N) {
        const int nb_bytes_remain = N-curr_byte;
        if (nb_header_bytes > nb_bytes_remain) {
            LOG_ERROR("[%d] fig 0/3 Insufficient length for header (%d/%d)\n",
                cif_index, nb_header_bytes, nb_bytes_remain);
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
            LOG_ERROR("[%d] fig 0/3 Insufficient length for CAOrg field (%d/%d)\n",
                cif_index, nb_data_length, nb_bytes_remain);
            return;
        }

        if (CAOrg_flag) {
            auto* v = &b[nb_header_bytes];
            CAOrg = 
                (static_cast<uint16_t>(v[0] & 0b11111111) << 8) |
                                     ((v[1] & 0b11111111) >> 0);
        }

        LOG_MESSAGE("[%d] fig 0/3 i=%d SCId=%u rfa=%u CAOrg=%u dg=%u rfu=%u DSCTy=%u subchannel_id=%u packet_address=%u CAOrg=%u\n", 
            cif_index,
            curr_component,
            SCId, rfa, CAOrg_flag, dg_flag, rfu, DSCTy, 
            subchannel_id, packet_address, CAOrg);
        
        handler->OnServiceComponent_2_PacketDataType(
            cif_index,
            SCId, subchannel_id, DSCTy, packet_address);
        
        curr_byte += nb_data_length;
        curr_component++;
    }
}

// Service components information in stream mode with conditional access
void FIG_Processor::ProcessFIG_Type_0_Ext_4(
    const FIG_Header_Type_0 header, 
    const uint8_t* buf, const uint8_t N, const int cif_index)
{
    const int nb_component_bytes = 3;
    if ((N % nb_component_bytes) != 0) {
        LOG_ERROR("[%d] fig 0/4 Field must be a multiple of %d bytes\n", 
            cif_index, nb_component_bytes);
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
        LOG_MESSAGE("[%d] fig 0/4 i=%d/%d rfa=%u rfu=%u subchannel_id=%u CAOrg=%u\n",
            cif_index,
            i, nb_components,
            rfa, rfu, subchannel_id, CAOrg);
        
        handler->OnServiceComponent_2_StreamConditionalAccess(
            cif_index,
            subchannel_id, CAOrg);
    }
}

// Service component language 
void FIG_Processor::ProcessFIG_Type_0_Ext_5(
    const FIG_Header_Type_0 header, 
    const uint8_t* buf, const uint8_t N, const int cif_index)
{
    int curr_byte = 0;
    while (curr_byte < N) {
        const int nb_remain_bytes = N-curr_byte;
        auto* b = &buf[curr_byte];

        // Long or short form flag
        const uint8_t LS =  (b[0] & 0b10000000) >> 7;
        const int nb_length_bytes = LS ? 3 : 2;

        if (nb_length_bytes > nb_remain_bytes) {
            LOG_ERROR("[%d] fig 0/5 LS=%u Insufficient length for contents (%d/%d)\n",
                cif_index, 
                LS, 
                nb_length_bytes, nb_remain_bytes);
            return;
        }

        // short form
        if (!LS) {
            const uint8_t Rfu =             (b[0] & 0b01000000) >> 6;
            const uint8_t subchannel_id =   (b[0] & 0b00111111) >> 0;
            const uint8_t language = b[1];
            LOG_MESSAGE("[%d] fig 0/5 LS=%u Rfu=%u subchannel_id=%-2u language=%u\n",
                cif_index,
                LS, 
                Rfu, subchannel_id, language);

            handler->OnServiceComponent_3_Short_Language(
                cif_index,
                subchannel_id, language);
        // long form
        } else {
            const uint8_t Rfa =             (b[0] & 0b01110000) >> 4;
            const uint16_t SCId =            
                      (static_cast<uint16_t>(b[0] & 0b00001111) << 8) |
                                           ((b[1] & 0b11111111) >> 0);
            const uint8_t language = b[2];
            LOG_MESSAGE("[%d] fig 0/5 LS=%u Rfa=%u SCId=%u language=%u\n",
                cif_index,
                LS, 
                Rfa, SCId, language);

            handler->OnServiceComponent_3_Long_Language(
                cif_index,
                SCId, language); 
        }

        curr_byte += nb_length_bytes;
    }
}

// Service linking information
void FIG_Processor::ProcessFIG_Type_0_Ext_6(
    const FIG_Header_Type_0 header, 
    const uint8_t* buf, const uint8_t N, const int cif_index)
{
    const int nb_header_bytes = 2;

    int curr_byte = 0;
    while (curr_byte < N) {
        const int nb_remain_bytes = N-curr_byte;

        // minimum of 16 bits = 2 bytes
        if (nb_remain_bytes < nb_header_bytes) {
            LOG_ERROR("[%d] fig 0/6 Insufficient length for header (%d/%d)\n",
                cif_index, nb_header_bytes, nb_remain_bytes);
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
            LOG_MESSAGE("[%d] fig 0/6 pd=%u ld=%u LA=%u S/H=%u ILS=%u LSN=%u\n",
                cif_index,
                header.pd,
                id_list_flag, is_active_link, is_hard_link, is_international, linkage_set_number);
            
            handler->OnServiceLinkage_1_LSN_Only(
                cif_index, 
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
            LOG_ERROR("[%d] fig 0/6 Insufficient length for long header (%d/%d)\n",
                cif_index, nb_total_header_bytes, nb_remain_bytes);
            return;
        }

        const uint8_t rfu0   = (b[2] & 0b10000000) >> 7;
        const uint8_t IdLQ   = (b[2] & 0b01100000) >> 5;
        const uint8_t Rfa0   = (b[2] & 0b00010000) >> 4;
        const uint8_t nb_ids = (b[2] & 0b00001111) >> 0;

        const int nb_list_remain = nb_remain_bytes-nb_total_header_bytes;
        if (nb_list_remain <= 0) {
            LOG_ERROR("[%d] fig 0/6 Insufficient length for any list buffer\n",
                cif_index);
            return;
        }

        // 3 possible arrangements for id list
        auto* list_buf = &b[3];

        // Arrangement 1: List of 16bit IDs
        if (!header.pd && !is_international) {
            const int nb_id_bytes = 2;
            const int nb_list_bytes = nb_id_bytes*nb_ids;
            if (nb_list_bytes > nb_list_remain) {
                LOG_ERROR("[%d] fig 0/6 Insufficient length for type 1 id list (%d/%d)\n",
                    cif_index, nb_list_bytes, nb_list_remain);
                return;
            }

            for (int i = 0; i < nb_ids; i++)  {
                auto* entry_buf = &list_buf[i*nb_id_bytes];

                // Interpret id according to value of IdLQ (id list qualifier) 
                switch (IdLQ) {
                case 0b00: // DAB service id - 16bit
                    {
                        ServiceIdentifier sid;
                        sid.ProcessShortForm(entry_buf);
                        LOG_MESSAGE("[%d] fig 0/6 pd=%u ld=%u LA=%u S/H=%u ILS=%u LSN=%u rfu0=%u IdLQ=%u Rfa0=%u type=1 i=%d/%d country_id=%u service_ref=%u ecc=%u\n",
                            cif_index,
                            header.pd,
                            id_list_flag, is_active_link, is_hard_link, is_international, linkage_set_number,
                            rfu0, IdLQ, Rfa0, 
                            i, nb_ids,
                            sid.country_id, sid.service_reference, sid.ecc);

                        handler->OnServiceLinkage_1_ServiceID(
                            cif_index,
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
                        LOG_MESSAGE("[%d] fig 0/6 pd=%u ld=%u LA=%u S/H=%u ILS=%u LSN=%u rfu0=%u IdLQ=%u Rfa0=%u type=1 i=%d/%d RDS_PI=%04X\n",
                            cif_index,
                            header.pd,
                            id_list_flag, is_active_link, is_hard_link, is_international, linkage_set_number,
                            rfu0, IdLQ, Rfa0, 
                            i, nb_ids,
                            rds_pi_code);
                        
                        handler->OnServiceLinkage_1_RDS_PI_ID(
                            cif_index,
                            is_active_link, is_hard_link, is_international,
                            linkage_set_number, rds_pi_code);
                    }
                    break;
                case 0b11: // DRM 24bit-service identifier
                    {
                        const uint32_t drm_id = 
                            (static_cast<uint32_t>(entry_buf[0]) << 8) |
                                                  (entry_buf[1]  << 0);
                        LOG_MESSAGE("[%d] fig 0/6 pd=%u ld=%u LA=%u S/H=%u ILS=%u LSN=%u rfu0=%u IdLQ=%u Rfa0=%u type=1 i=%d/%d DRM_id=%u\n",
                            cif_index,
                            header.pd,
                            id_list_flag, is_active_link, is_hard_link, is_international, linkage_set_number,
                            rfu0, IdLQ, Rfa0, 
                            i, nb_ids,
                            drm_id);
                        
                        handler->OnServiceLinkage_1_DRM_ID(
                            cif_index,
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
                LOG_ERROR("[%d] fig 0/6 Insufficient length for type 2 id list (%d/%d)\n",
                    cif_index, nb_list_bytes, nb_list_remain);
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
                        sid.ProcessShortForm(&entry_buf[1]);
                        sid.ecc = ecc;
                        LOG_MESSAGE("[%d] fig 0/6 pd=%u ld=%u LA=%u S/H=%u ILS=%u LSN=%u rfu0=%u IdLQ=%u Rfa0=%u type=2 i=%d/%d country_id=%u service_ref=%u ecc=%u\n",
                            cif_index,
                            header.pd,
                            id_list_flag, is_active_link, is_hard_link, is_international, linkage_set_number,
                            rfu0, IdLQ, Rfa0, 
                            i, nb_ids,
                            sid.country_id, sid.service_reference, sid.ecc);
                        
                        handler->OnServiceLinkage_1_ServiceID(
                            cif_index,
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
                        LOG_MESSAGE("[%d] fig 0/6 pd=%u ld=%u LA=%u S/H=%u ILS=%u LSN=%u rfu0=%u IdLQ=%u Rfa0=%u type=2 i=%d/%d RDS_PI=%04X ecc=%u\n",
                            cif_index,
                            header.pd,
                            id_list_flag, is_active_link, is_hard_link, is_international, linkage_set_number,
                            rfu0, IdLQ, Rfa0, 
                            i, nb_ids,
                            rds_pi_code, ecc);
                        
                        handler->OnServiceLinkage_1_RDS_PI_ID(
                            cif_index,
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
                        LOG_MESSAGE("[%d] fig 0/6 pd=%u ld=%u LA=%u S/H=%u ILS=%u LSN=%u rfu0=%u IdLQ=%u Rfa0=%u type=2 i=%d/%d DRM_id=%u\n",
                            cif_index,
                            header.pd,
                            id_list_flag, is_active_link, is_hard_link, is_international, linkage_set_number,
                            rfu0, IdLQ, Rfa0, 
                            i, nb_ids,
                            drm_id);
                        
                        handler->OnServiceLinkage_1_DRM_ID(
                            cif_index,
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
                LOG_ERROR("[%d] fig 0/6 Insufficient length for type 3 id list (%d/%d)\n",
                    cif_index, nb_list_bytes, nb_list_remain);
                return;
            }
            for (int i = 0; i < nb_ids; i++)  {
                auto* entry_buf = &list_buf[i*nb_entry_bytes];

                // Interpret id according to value of IdLQ (id list qualifier) 
                switch (IdLQ) {
                case 0b00: // DAB service id - 32bit 
                    {
                        ServiceIdentifier sid;
                        sid.ProcessLongForm(entry_buf);
                        LOG_MESSAGE("[%d] fig 0/6 pd=%u ld=%u LA=%u S/H=%u ILS=%u LSN=%u rfu0=%u IdLQ=%u Rfa0=%u type=3 i=%d/%d country_id=%u service_ref=%u ecc=%u\n",
                            cif_index,
                            header.pd,
                            id_list_flag, is_active_link, is_hard_link, is_international, linkage_set_number,
                            rfu0, IdLQ, Rfa0, 
                            i, nb_ids,
                            sid.country_id, sid.service_reference, sid.ecc);
                        
                        handler->OnServiceLinkage_1_ServiceID(
                            cif_index,
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
                        LOG_MESSAGE("[%d] fig 0/6 pd=%u ld=%u LA=%u S/H=%u ILS=%u LSN=%u rfu0=%u IdLQ=%u Rfa0=%u type=3 i=%d/%d RDS_PI=%08X\n",
                            cif_index,
                            header.pd,
                            id_list_flag, is_active_link, is_hard_link, is_international, linkage_set_number,
                            rfu0, IdLQ, Rfa0, 
                            i, nb_ids,
                            rds_pi_code);
                        
                        handler->OnServiceLinkage_1_RDS_PI_ID(
                            cif_index,
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
                        LOG_MESSAGE("[%d] fig 0/6 pd=%u ld=%u LA=%u S/H=%u ILS=%u LSN=%u rfu0=%u IdLQ=%u Rfa0=%u type=3 i=%d/%d DRM_id=%u\n",
                            cif_index,
                            header.pd,
                            id_list_flag, is_active_link, is_hard_link, is_international, linkage_set_number,
                            rfu0, IdLQ, Rfa0, 
                            i, nb_ids,
                            drm_id);
                        
                        handler->OnServiceLinkage_1_DRM_ID(
                            cif_index,
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
void FIG_Processor::ProcessFIG_Type_0_Ext_7(
    const FIG_Header_Type_0 header, 
    const uint8_t* buf, const uint8_t N, const int cif_index)
{
    const int nb_data_bytes = 2;
    if (N != nb_data_bytes) {
        LOG_ERROR("[%d] fig 0/7 Length doesn't match expectations (%d/%d)\n",
            cif_index, N, nb_data_bytes);
        return;
    }

    const uint8_t nb_services = (buf[0] & 0b11111100) >> 2;
    const uint16_t reconfiguration_count = 
          (static_cast<uint16_t>(buf[0] & 0b00000011) << 8) |
                               ((buf[1] & 0b11111111) >> 0);
    
    LOG_MESSAGE("[%d] fig 0/7 total_services=%u reconfiguration_count=%u\n",
        cif_index,
        nb_services, reconfiguration_count);
    
    handler->OnConfigurationInformation_1(
        cif_index,
        nb_services, reconfiguration_count);
}

// Service component global definition 
void FIG_Processor::ProcessFIG_Type_0_Ext_8(
    const FIG_Header_Type_0 header, 
    const uint8_t* buf, const uint8_t N, const int cif_index)
{
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
            LOG_ERROR("[%d] fig 0/8 Message not long enough for header field (%d)\n",
                cif_index, nb_remain_bytes);
            return;
        }

        ServiceIdentifier sid;
        if (!header.pd) {
            sid.ProcessShortForm(service_buf);
        } else {
            sid.ProcessLongForm(service_buf);
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
            LOG_ERROR("[%d] fig 0/8 Message not long enough for tail data (%d/%d)\n", 
                cif_index, nb_length_bytes, nb_remain_bytes);
            return;
        }

        const uint8_t rfa2 = ext_flag ? data_buf[nb_data_bytes] : 0x00; 

        if (!ls_flag) {
            const uint8_t rfu0          = (data_buf[0] & 0b01000000) >> 6;
            const uint8_t subchannel_id = (data_buf[0] & 0b00111111) >> 0;
            LOG_MESSAGE("[%d] fig 0/8 pd=%u country_id=%-2u service_ref=%-4u ecc=%u ext=%u rfa0=%u SCIdS=%u is_long=%u rfu0=%u subchannel_id=%-2u rfa2=%u\n",
                cif_index, 
                header.pd, 
                sid.country_id, sid.service_reference, sid.ecc, 
                ext_flag, rfa0, SCIdS,
                ls_flag, rfu0, subchannel_id, rfa2);
            
            handler->OnServiceComponent_4_Short_Definition(
                cif_index,
                sid.country_id, sid.service_reference, sid.ecc,
                SCIdS, subchannel_id);
        } else {
            const uint8_t rfa1 =          (data_buf[0] & 0b01110000) >> 4;
            const uint16_t SCId = 
                    (static_cast<uint16_t>(data_buf[0] & 0b00001111) << 8) |
                                         ((data_buf[1] & 0b11111111) >> 0);
            LOG_MESSAGE("[%d] fig 0/8 pd=%u country_id=%-2u service_ref=%-4u ecc=%u ext=%u rfa0=%u SCIdS=%u is_long=%u rfa1=%u SCId=%-2u rfa2=%u\n",
                cif_index, 
                header.pd, 
                sid.country_id, sid.service_reference, sid.ecc, 
                ext_flag, rfa0, SCIdS,
                ls_flag, rfa1, SCId, rfa2);
            
            handler->OnServiceComponent_4_Long_Definition(
                cif_index,
                sid.country_id, sid.service_reference, sid.ecc,
                SCIdS, SCId);
        }

        // Move to the next service
        curr_index += nb_length_bytes;
        curr_service++;
    }
}

// Country, LTO and International Table
void FIG_Processor::ProcessFIG_Type_0_Ext_9(
    const FIG_Header_Type_0 header, 
    const uint8_t* buf, const uint8_t N, const int cif_index)
{
    const int nb_header_bytes = 3;
    if (nb_header_bytes > N) {
        LOG_ERROR("[%d] fig 0/9 Insufficient length for header (%d/%d)\n",
            cif_index, nb_header_bytes, N);
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
        LOG_ERROR("[%d] fig 0/9 Insufficient length for extended field (%d)\n",
            cif_index, nb_ext_bytes);
        return;
    }

    if (!ext_flag && (nb_ext_bytes > 0)) {
        LOG_ERROR("[%d] fig 0/9 Extra bytes unaccounted for no extended fields (%d)\n",
            cif_index, nb_ext_bytes);
        return;
    }

    // no extended field
    if (!ext_flag) {
        LOG_MESSAGE("[%d] fig 0/9 ext=%u Rfa1=%u ensemble_lto=%u ensemble_ecc=%02X inter_table_id=%u\n",
            cif_index, 
            ext_flag, Rfa1, ensemble_lto, ensemble_ecc, inter_table_id);
        
        handler->OnEnsemble_2_Country(
            cif_index, 
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
            LOG_ERROR("[%d] fig 0/9 Insufficient length for subfield header (%d/%d)\n",
                cif_index, nb_subfield_header_bytes, nb_ext_remain_bytes);
            return;
        }

        auto* subfield_buf = &extended_buf[curr_byte];
        const uint8_t nb_services = (subfield_buf[0] & 0b11000000) >> 6;
        const uint8_t Rfa2 =        (subfield_buf[0] & 0b00111111) >> 0;
        const uint8_t service_ecc = (subfield_buf[1] & 0b11111111) >> 0;

        const int nb_remain_list_bytes = nb_ext_remain_bytes-nb_subfield_header_bytes;
        const int nb_list_bytes = nb_services*nb_service_id_bytes;

        if (nb_list_bytes > nb_remain_list_bytes) {
            LOG_ERROR("[%d] fig 0/9 Insufficient length for service id list (%d/%d)\n",
                cif_index, nb_list_bytes, nb_remain_list_bytes);
            return;
        }

        auto* service_ids_buf = &subfield_buf[nb_subfield_header_bytes];
        for (int i = 0; i < nb_services; i++) {
            auto* b = &service_ids_buf[i*nb_service_id_bytes];
            ServiceIdentifier sid;
            sid.ProcessShortForm(b);
            sid.ecc = service_ecc;
            LOG_MESSAGE("[%d] fig 0/9 ext=%u Rfa1=%u ensemble_lto=%u ensemble_ecc=%u inter_table_id=%u Rfa2=%u ECC=%u i=%d-%d/%d service_country_id=%u service_ref=%u service_ecc=%u\n",
                cif_index,
                ext_flag, Rfa1, ensemble_lto, ensemble_ecc, inter_table_id,
                Rfa2, service_ecc, 
                curr_subfield, i, nb_services,
                sid.country_id, sid.service_reference, sid.ecc);
            
            handler->OnEnsemble_2_Service_Country(
                cif_index,
                ensemble_lto, ensemble_ecc, inter_table_id,
                sid.country_id, sid.service_reference, sid.ecc);
        }

        curr_subfield++;
        curr_byte += (nb_subfield_header_bytes + nb_list_bytes);
    }
}

// Date and time
void FIG_Processor::ProcessFIG_Type_0_Ext_10(
    const FIG_Header_Type_0 header, 
    const uint8_t* buf, const uint8_t N, const int cif_index)
{
    const int nb_min_bytes = 4;
    if (nb_min_bytes > N) {
        LOG_ERROR("[%d] fig 0/10 Insufficient length for minimum configuration (%d/%d)\n",
            cif_index, nb_min_bytes, N);
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
        LOG_ERROR("[%d] fig 0/10 Insufficient length for long form UTC (%d/%d)\n",
            cif_index, nb_actual_bytes, N);
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

    LOG_MESSAGE("[%d] fig 0/10 rfu0=%u MJD=%u LSI=%u Rfa0=%u UTC=%u time=%02u:%02u:%02u.%03u\n",
        cif_index,
        rfu0, 
        MJD,
        LSI, Rfa0, UTC,
        hours, minutes, seconds, milliseconds);
    
    handler->OnDateTime_1(
        cif_index,
        MJD, hours, minutes, seconds, milliseconds,
        LSI, UTC);
}

// User application information
void FIG_Processor::ProcessFIG_Type_0_Ext_13(
    const FIG_Header_Type_0 header, 
    const uint8_t* buf, const uint8_t N, const int cif_index)
{
    const int nb_service_id_bytes = header.pd ? 4 : 2;
    // In addition to the service id field, we have an additional byte of fields
    const int nb_header_bytes = nb_service_id_bytes+1;

    int curr_byte = 0;
    int curr_block = 0;
    while (curr_byte != N) {
        const int nb_remain_bytes = N-curr_byte;
        if (nb_header_bytes > nb_remain_bytes) {
            LOG_ERROR("[%d] fig 0/13 Length not long enough for header data (%d)\n", 
                cif_index, nb_remain_bytes);
            return;
        }

        auto* entity_buf = &buf[curr_byte];

        ServiceIdentifier sid;
        if (!header.pd) {
            sid.ProcessShortForm(entity_buf);
        } else {
            sid.ProcessLongForm(entity_buf);
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
                LOG_ERROR("[%d] fig 0/13 Length not long enough for app header data (%d/%d)\n",
                    cif_index, nb_app_header_bytes, nb_app_remain_bytes);
                return;
            }

            const uint16_t user_app_type = 
                        (static_cast<uint16_t>(app_buf[0] & 0b11111111) << 3) |
                                             ((app_buf[1] & 0b11100000) >> 5);
            
            // Length of XPAD and user app data field
            const uint8_t nb_app_data_bytes = (app_buf[1] & 0b00011111) >> 0;

            const uint8_t nb_app_total_bytes = nb_app_header_bytes + nb_app_data_bytes;
            if (nb_app_total_bytes > nb_app_remain_bytes) {
                LOG_ERROR("[%d] fig 0/13 Length not long enough for app XPAD/user data (%d/%d)\n",
                    cif_index, nb_app_total_bytes, nb_app_remain_bytes);
                return;
            }

            // TODO: process this app data somehow
            // Sometimes it is XPAD data, sometimes it isn't
            LOG_MESSAGE("[%d] fig 0/13 pd=%u country_id=%-2u service_ref=%-4u ecc=%u SCIdS=%u i=%d-%d/%d app_type=%u L=%u\n",
                cif_index,
                header.pd,
                sid.country_id, sid.service_reference, sid.ecc,
                SCIdS, 
                curr_block, i, nb_user_apps, 
                user_app_type, nb_app_data_bytes);

            auto* app_data_buf = (nb_app_data_bytes > 0) ? &app_buf[nb_app_header_bytes] : NULL;
            handler->OnServiceComponent_5_UserApplication(
                cif_index,
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
void FIG_Processor::ProcessFIG_Type_0_Ext_14(
    const FIG_Header_Type_0 header, 
    const uint8_t* buf, const uint8_t N, const int cif_index)
{

    for (int i = 0; i < N; i++) {
        const uint8_t v = buf[i];
        const uint8_t subchannel_id = (v & 0b11111100) >> 2;
        const uint8_t fec           = (v & 0b00000011) >> 0;
        LOG_MESSAGE("[%d] fig 0/14 i=%d/%d id=%-2d fec=%d\n",
            cif_index,
            i, N,
            subchannel_id, fec);
        
        handler->OnSubchannel_2_FEC(
            cif_index,
            subchannel_id, fec);
    }
}

// Programme type
void FIG_Processor::ProcessFIG_Type_0_Ext_17(
    const FIG_Header_Type_0 header, 
    const uint8_t* buf, const uint8_t N, const int cif_index)
{
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
            LOG_ERROR("[%d] fig 0/17 Remaining buffer doesn't have minimum bytes (%d/%d)\n",
                cif_index, nb_min_bytes, nb_remain_bytes);
            return;
        }

        ServiceIdentifier sid;
        sid.ProcessShortForm(b);

        // const uint8_t SD =    (b[2] & 0b10000000) >> 7;
        // const uint8_t Rfa1 =  (b[2] & 0b01000000) >> 6;
        // const uint8_t Rfu1 =  (b[2] & 0b00110000) >> 4;
        // const uint8_t Rfa2 = ((b[2] & 0b00001111) << 2) |
        //                      ((b[4] & 0b11000000) >> 6);
        // const uint8_t Rfu2 =  (b[4] & 0b00100000) >> 5;
        // const uint8_t international_code = 
        //                       (b[4] & 0b00011111) >> 0;

        const uint8_t SD =            (b[2] & 0b10000000) >> 7;
        const uint8_t language_flag = (b[2] & 0b00100000) >> 5;
        const uint8_t cc_flag =       (b[2] & 0b00010000) >> 4;

        uint8_t language_type = 0;
        uint8_t cc_type = 0;


        const int nb_bytes = nb_min_bytes + language_flag + cc_flag;
        int data_index = 3;

        if (nb_remain_bytes < nb_bytes) {
            LOG_ERROR("[%d] fig 0/17 Insufficient bytes for langugage (%d) and caption (%d) field (%d/%d)\n",
                cif_index, 
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
        
        // LOG_MESSAGE("[%d] fig 0/17 pd=%u country_id=%u service_ref=%-4u ecc=%u i=%d/%d SD=%u Rfa1=%u Rfu1=%u Rfa2=%u Rfu2=%u inter_code=%u\n",
        //     cif_index,
        //     header.pd,
        //     sid.country_id, sid.service_reference, sid.ecc,
        //     i, nb_programmes,
        //     SD, Rfa1, Rfu1, Rfa2, Rfu2, 
        //     international_code);

        LOG_MESSAGE("[%d] fig 0/17 pd=%u country_id=%u service_ref=%-4u ecc=%u i=%d SD=%u L_flag=%u cc_flag=%u inter_code=%-2u language=%u CC=%u\n",
            cif_index,
            header.pd,
            sid.country_id, sid.service_reference, sid.ecc,
            curr_programme,
            SD, language_flag, cc_flag, 
            international_code,
            language_type, cc_type);
        
        handler->OnService_1_ProgrammeType(
            cif_index,
            sid.country_id, sid.service_reference, sid.ecc, 
            international_code, language_type, cc_type,
            language_flag, cc_flag);

        curr_byte += nb_bytes; 
        curr_programme++;
    }
}

// Frequency information
void FIG_Processor::ProcessFIG_Type_0_Ext_21(
    const FIG_Header_Type_0 header, 
    const uint8_t* buf, const uint8_t N, const int cif_index)
{
    const int nb_block_header_bytes = 2;

    // We have a list of blocks
    // Each block contains a list of frequency information lists
    // Each frequency information list contains different types of ids depending on RM field

    int curr_byte = 0;
    int curr_block = 0;
    while (curr_byte < N) {
        const int nb_remain_bytes = N-curr_byte;
        if (nb_block_header_bytes > nb_remain_bytes) {
            LOG_ERROR("[%d] fig 0/21 Insufficient length for block header (%d/%d)\n",
                cif_index,
                nb_block_header_bytes, nb_remain_bytes);
            return;
        }

        // increment this to update the block size
        int nb_block_bytes = nb_block_header_bytes;
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
                LOG_ERROR("[%d] fig 0/21 Insufficient length for fi list header (%d/%d)\n",
                    cif_index,
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
                        LOG_ERROR("[%d] fig 0/21 Frequency list RM=%u doesn't have a list length that is a multiple (%dmod%d)\n",
                            cif_index,
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

                        LOG_MESSAGE("[%d] fig 0/21 i=%d-%d-%d/%d Rfa0=%u RM=%u is_continuous=%u country_id=%u ensemble_ref=%u is_adjacent=%u is_mode_I=%u freq=%.3fMHz\n",
                            cif_index,
                            curr_block, curr_fi_list, i, nb_entries, 
                            Rfa0, RM, 
                            is_continuous_output, 
                            eid.country_id, eid.ensemble_reference,
                            is_geographically_adjacent, is_transmission_mode_I,
                            (float)(alt_freq)*1e-6f);
                        
                        handler->OnFrequencyInformation_1_Ensemble(
                            cif_index,
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
                        LOG_MESSAGE("[%d] fig 0/21 i=%d-%d-%d/%d Rfa0=%u RM=%u time_compensated=%u RDS_PI=%04X freq=%.3fMHz\n",
                            cif_index,
                            curr_block, curr_fi_list, i, nb_freq_list_bytes, 
                            Rfa0, RM, is_time_compensated, 
                            rds_pi_code,
                            (float)(alt_freq)*1e-6f);
                        
                        handler->OnFrequencyInformation_1_RDS_PI(
                            cif_index,
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
                        LOG_ERROR("[%d] fig 0/21 Frequency list RM=%u doesn't have a list length that is a multiple (%dmod%d)\n",
                            cif_index,
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

                        LOG_MESSAGE("[%d] fig 0/21 i=%d-%d-%d/%d Rfa0=%u RM=%u time_compensated=%u DRM_id=%u freq=%.3fMHz\n",
                            cif_index,
                            curr_block, curr_fi_list, i, nb_entries, 
                            Rfa0, RM, is_time_compensated, 
                            drm_id, (float)(alt_freq)*1e-6f);
                        
                        handler->OnFrequencyInformation_1_DRM(
                            cif_index,
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
                        LOG_ERROR("[%d] fig 0/21 Frequency list RM=%u doesn't have a list length that is a multiple (%dmod%d)\n",
                            cif_index,
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

                        LOG_MESSAGE("[%d] fig 0/21 i=%d-%d-%d/%d Rfa0=%u RM=%u time_compensated=%u AMSS_id=%u freq=%.3fMHz\n",
                            cif_index,
                            curr_block, curr_fi_list, i, nb_entries, 
                            Rfa0, RM, is_time_compensated, 
                            amss_id, (float)(alt_freq)*1e-6f);
                        
                        handler->OnFrequencyInformation_1_AMSS(
                            cif_index,
                            amss_id, alt_freq, is_time_compensated);
                    }
                }
            default:
                LOG_ERROR("[%d] fig 0/21 Unknown RM value (%u)\n", cif_index, RM);
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
void FIG_Processor::ProcessFIG_Type_0_Ext_24(
    const FIG_Header_Type_0 header, 
    const uint8_t* buf, const uint8_t N, const int cif_index)
{
    const int nb_sid_bytes = header.pd ? 4 : 2;
    const int nb_header_bytes = nb_sid_bytes + 1;

    int curr_byte = 0;
    while (curr_byte < N) {
        const int nb_remain_bytes = N-curr_byte;
        if (nb_header_bytes > nb_remain_bytes) {
            LOG_ERROR("[%d] fig 0/24 Insufficient length for header bytes (%d/%d)\n",
                cif_index, nb_header_bytes, nb_remain_bytes);
            return;
        }

        auto* b = &buf[curr_byte];

        ServiceIdentifier sid;
        if (!header.pd) {
            sid.ProcessShortForm(b);
        } else {
            sid.ProcessLongForm(b);
        }

        const uint8_t descriptor = b[nb_sid_bytes];
        const uint8_t Rfa =     (descriptor & 0b10000000) >> 7;
        const uint8_t CAId =    (descriptor & 0b01110000) >> 4;
        const uint8_t nb_EIds = (descriptor & 0b00001111) >> 0;

        const int nb_EId_bytes = 2;
        const int nb_EId_list_bytes = nb_EId_bytes*nb_EIds;
        const int nb_EId_list_remain_bytes = nb_remain_bytes - nb_header_bytes;

        if (nb_EId_list_bytes > nb_EId_list_remain_bytes) {
            LOG_ERROR("[%d] fig 0/24 Insufficient length for EId list (%d/%d)\n",
                cif_index, nb_EId_list_bytes, nb_EId_list_remain_bytes);
            return;
        }

        auto* eids_buf = &b[nb_header_bytes];
        for (int i = 0; i < nb_EIds; i++) {
            auto* eid_buf = &eids_buf[i*nb_EId_bytes];
            EnsembleIdentifier eid;
            eid.ProcessBuffer(eid_buf);

            LOG_MESSAGE("[%d] fig 0/24 country_id=%u service_ref=%u ecc=%u Rfa=%u CAId=%u i=%d/%d ensemble_country_id=%u ensemble_reference=%u\n",
                cif_index,
                sid.country_id, sid.service_reference, sid.ecc,
                Rfa, CAId, i, nb_EIds,
                eid.country_id, eid.ensemble_reference);
            
            handler->OnOtherEnsemble_1_Service(
                cif_index,
                sid.country_id, sid.service_reference, sid.ecc,
                eid.country_id, eid.ensemble_reference);
        }
        curr_byte += (nb_header_bytes + nb_EId_list_bytes);
    }
}

// Ensemble label
void FIG_Processor::ProcessFIG_Type_1_Ext_0(
    const FIG_Header_Type_1 header, 
    const uint8_t* buf, const uint8_t N, const int cif_index)
{

    const int nb_eid_bytes = 2;
    const int nb_char_bytes = 16;
    const int nb_flag_bytes = 2;
    const int nb_expected_bytes = nb_eid_bytes + nb_char_bytes + nb_flag_bytes;

    if (N != nb_expected_bytes) {
        LOG_ERROR("[%d] fig 1/0 Expected %d bytes got %d bytes\n",
            cif_index, nb_expected_bytes, N);
        return;
    }

    EnsembleIdentifier eid;
    eid.ProcessBuffer(buf);

    auto* char_buf = &buf[nb_eid_bytes];
    // flag field is used for determining which characters can be removed
    // when we are abbreviating the label
    const int flag_index = nb_eid_bytes + nb_char_bytes;
    const uint16_t flag_field = 
        (static_cast<uint16_t>(buf[flag_index+0]) << 8) | 
                               buf[flag_index+1];
    
    LOG_MESSAGE("[%d] fig 1/0 charset=%u country_id=%u ensemble_ref=%-4u flag=%04X chars=%.*s\n",
        cif_index, header.charset,
        eid.country_id, eid.ensemble_reference,
        flag_field,
        nb_char_bytes, char_buf);
    
    handler->OnEnsemble_3_Label(
        cif_index,
        eid.country_id, eid.ensemble_reference,
        flag_field, char_buf, nb_char_bytes);
}

// Short form service identifier label
void FIG_Processor::ProcessFIG_Type_1_Ext_1(
    const FIG_Header_Type_1 header, 
    const uint8_t* buf, const uint8_t N, const int cif_index)
{
    const int nb_sid_bytes = 2;
    const int nb_char_bytes = 16;
    const int nb_flag_bytes = 2;
    const int nb_expected_bytes = nb_sid_bytes + nb_char_bytes + nb_flag_bytes;

    if (N != nb_expected_bytes) {
        LOG_ERROR("[%d] fig 1/1 Expected %d bytes got %d bytes\n",
            cif_index, nb_expected_bytes, N);
        return;
    }

    ServiceIdentifier sid;
    sid.ProcessShortForm(buf);

    auto* char_buf = &buf[nb_sid_bytes];
    const int flag_index = nb_sid_bytes + nb_char_bytes;
    const uint16_t flag_field = 
        (static_cast<uint16_t>(buf[flag_index+0]) << 8) | 
                               buf[flag_index+1];
    
    LOG_MESSAGE("[%d] fig 1/1 charset=%u country_id=%u service_ref=%-4u ecc=%u flag=%04X chars=%.*s\n",
        cif_index, header.charset,
        sid.country_id, sid.service_reference, sid.ecc,
        flag_field,
        nb_char_bytes, char_buf);

    handler->OnService_2_Label(
        cif_index,
        sid.country_id, sid.service_reference, sid.ecc,
        flag_field, char_buf, nb_char_bytes);
}

// Service component label (non primary)
void FIG_Processor::ProcessFIG_Type_1_Ext_4(
    const FIG_Header_Type_1 header, 
    const uint8_t* buf, const uint8_t N, const int cif_index)
{
    const int nb_header_bytes = 1;
    const int nb_char_bytes = 16;
    const int nb_flag_bytes = 2;

    if (N < nb_header_bytes) {
        LOG_ERROR("[%d] fig 1/4 Expected at least %d byte for header got %d bytes\n",
            cif_index, nb_header_bytes, N);
        return;
    }

    const uint8_t descriptor = buf[0];
    const uint8_t pd =    (descriptor & 0b10000000) >> 7;
    const uint8_t Rfa =   (descriptor & 0b01110000) >> 4;
    const uint8_t SCIdS = (descriptor & 0b00001111) >> 0;

    const int nb_sid_bytes = pd ? 4 : 2;
    const int nb_expected_bytes = nb_header_bytes + nb_sid_bytes + nb_char_bytes + nb_flag_bytes;

    if (N != nb_expected_bytes) {
        LOG_ERROR("[%d] fig 1/4 Expected %d bytes got %d bytes\n",
            cif_index, nb_expected_bytes, N);
        return;
    }

    ServiceIdentifier sid;
    if (!pd) {
        sid.ProcessShortForm(&buf[nb_header_bytes]);
    } else {
        sid.ProcessLongForm(&buf[nb_header_bytes]);
    }

    // iterated backwards
    auto* char_buf = &buf[nb_header_bytes+nb_sid_bytes];

    const int flag_index = nb_header_bytes + nb_sid_bytes + nb_char_bytes;
    const uint16_t flag_field = 
        (static_cast<uint16_t>(buf[flag_index+0]) << 8) | 
                               buf[flag_index+1];
    
    LOG_MESSAGE("[%d] fig 1/5 charset=%u SCIdS=%u country_id=%u service_ref=%-4u ecc=%u flag=%04X chars=%.*s\n",
        cif_index, header.charset,
        SCIdS,
        sid.country_id, sid.service_reference, sid.ecc,
        flag_field,
        nb_char_bytes, char_buf);

    handler->OnServiceComponent_6_Label(
        cif_index,
        sid.country_id, sid.service_reference, sid.ecc,
        SCIdS, 
        flag_field, char_buf, nb_char_bytes);
}

// Long form service identifier label
void FIG_Processor::ProcessFIG_Type_1_Ext_5(
    const FIG_Header_Type_1 header, 
    const uint8_t* buf, const uint8_t N, const int cif_index)
{
    const int nb_sid_bytes = 4;
    const int nb_char_bytes = 16;
    const int nb_flag_bytes = 2;

    const int nb_expected_bytes = nb_sid_bytes + nb_char_bytes + nb_flag_bytes;
    if (N != nb_expected_bytes) {
        LOG_ERROR("[%d] fig 1/5 Expected %d bytes got %d bytes\n",
            cif_index, nb_expected_bytes, N);
        return;
    }

    ServiceIdentifier sid;
    sid.ProcessLongForm(buf);

    auto* char_buf = &buf[nb_sid_bytes];
    const int flag_index = nb_sid_bytes + nb_char_bytes;
    const uint16_t flag_field = 
        (static_cast<uint16_t>(buf[flag_index+0]) << 8) | 
                               buf[flag_index+1];
    
    LOG_MESSAGE("[%d] fig 1/5 charset=%u country_id=%u service_ref=%-4u ecc=%u flag=%04X chars=%.*s\n",
        cif_index, header.charset,
        sid.country_id, sid.service_reference, sid.ecc,
        flag_field,
        nb_char_bytes, char_buf);
    
    handler->OnService_2_Label(
        cif_index,
        sid.country_id, sid.service_reference, sid.ecc,
        flag_field, char_buf, nb_char_bytes);
}
