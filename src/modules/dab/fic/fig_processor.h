#pragma once

#include <stdint.h>

class FIG_Handler_Interface;

class FIG_Processor
{
private:
    struct FIG_Header_Type_0 {
        uint8_t cn;
        uint8_t oe;
        uint8_t pd;
    };
    struct FIG_Header_Type_1 {
        uint8_t charset;
        uint8_t rfu;
    };
    struct FIG_Header_Type_2 {
        uint8_t toggle_flag;
        uint8_t segment_index;
        uint8_t rfu;
    };
    FIG_Handler_Interface* handler;
public:
    void ProcessFIB(const uint8_t* buf);
    inline void SetHandler(FIG_Handler_Interface* _handler) { handler = _handler; }
private:
    // handle each type
    void ProcessFIG_Type_0(
        const uint8_t* buf, const uint8_t N);
    void ProcessFIG_Type_1(const uint8_t* buf, const uint8_t N);
    void ProcessFIG_Type_2(const uint8_t* buf, const uint8_t N);
    void ProcessFIG_Type_6(const uint8_t* buf, const uint8_t N);
    // handle fig 0/X
    void ProcessFIG_Type_0_Ext_0 (const FIG_Header_Type_0 header, const uint8_t* buf, const uint8_t N);
    void ProcessFIG_Type_0_Ext_1 (const FIG_Header_Type_0 header, const uint8_t* buf, const uint8_t N);
    void ProcessFIG_Type_0_Ext_2 (const FIG_Header_Type_0 header, const uint8_t* buf, const uint8_t N);
    void ProcessFIG_Type_0_Ext_3 (const FIG_Header_Type_0 header, const uint8_t* buf, const uint8_t N);
    void ProcessFIG_Type_0_Ext_4 (const FIG_Header_Type_0 header, const uint8_t* buf, const uint8_t N);
    void ProcessFIG_Type_0_Ext_5 (const FIG_Header_Type_0 header, const uint8_t* buf, const uint8_t N);
    void ProcessFIG_Type_0_Ext_6 (const FIG_Header_Type_0 header, const uint8_t* buf, const uint8_t N);
    void ProcessFIG_Type_0_Ext_7 (const FIG_Header_Type_0 header, const uint8_t* buf, const uint8_t N);
    void ProcessFIG_Type_0_Ext_8 (const FIG_Header_Type_0 header, const uint8_t* buf, const uint8_t N);
    void ProcessFIG_Type_0_Ext_9 (const FIG_Header_Type_0 header, const uint8_t* buf, const uint8_t N);
    void ProcessFIG_Type_0_Ext_10(const FIG_Header_Type_0 header, const uint8_t* buf, const uint8_t N);
    void ProcessFIG_Type_0_Ext_13(const FIG_Header_Type_0 header, const uint8_t* buf, const uint8_t N);
    void ProcessFIG_Type_0_Ext_14(const FIG_Header_Type_0 header, const uint8_t* buf, const uint8_t N);
    void ProcessFIG_Type_0_Ext_17(const FIG_Header_Type_0 header, const uint8_t* buf, const uint8_t N);
    void ProcessFIG_Type_0_Ext_21(const FIG_Header_Type_0 header, const uint8_t* buf, const uint8_t N);
    void ProcessFIG_Type_0_Ext_24(const FIG_Header_Type_0 header, const uint8_t* buf, const uint8_t N);
    // handle fig 1/X
    void ProcessFIG_Type_1_Ext_0 (const FIG_Header_Type_1 header, const uint8_t* buf, const uint8_t N);
    void ProcessFIG_Type_1_Ext_1 (const FIG_Header_Type_1 header, const uint8_t* buf, const uint8_t N);
    void ProcessFIG_Type_1_Ext_4 (const FIG_Header_Type_1 header, const uint8_t* buf, const uint8_t N);
    void ProcessFIG_Type_1_Ext_5 (const FIG_Header_Type_1 header, const uint8_t* buf, const uint8_t N);
};