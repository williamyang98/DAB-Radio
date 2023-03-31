#pragma once
#include <stdint.h>
#include "../database/dab_database_entities.h"

// DOC: ETSI EN 300 401
// The following tables and constants were taken from the above doc
struct UEP_Descriptor {
    static constexpr int TOTAL_PUNCTURE_CODES = 4;
    uint16_t subchannel_size; 
    uint16_t bitrate;
    uint8_t protection_level;
    uint8_t Lx[TOTAL_PUNCTURE_CODES];   // Number of 128bit blocks with that puncture code
    uint8_t PIx[TOTAL_PUNCTURE_CODES];  // ID of the puncture code
    uint8_t total_padding_bits;         // UEP is different to EEP in that it adds padding bits
};

constexpr int UEP_PROTECTION_TABLE_SIZE = 64;

// Combination of Table 8 (subchannel size, bitrate and protection level) 
//            and Table 15 (puncture codes and padding bits)
const UEP_Descriptor UEP_PROTECTION_TABLE[UEP_PROTECTION_TABLE_SIZE] = {
    {  16,  32, 5, { 3,  4,  17, 0}, { 5,  3,  2,  0}, 0 },
    {  21,  32, 4, { 3,  3,  18, 0}, {11,  6,  5,  0}, 0 },
    {  24,  32, 3, { 3,  4,  14, 3}, {15,  9,  6,  8}, 0 },
    {  29,  32, 2, { 3,  4,  14, 3}, {22, 13,  8, 13}, 0 },
    {  35,  32, 1, { 3,  5,  13, 3}, {24, 17, 12, 17}, 4 },
    {  24,  48, 5, { 4,  3,  26, 3}, { 5,  4,  2,  3}, 0 },
    {  29,  48, 4, { 3,  4,  26, 3}, { 9,  6,  4,  6}, 0 },
    {  35,  48, 3, { 3,  4,  26, 3}, {15, 10,  6,  9}, 4 },
    {  42,  48, 2, { 3,  4,  26, 3}, {24, 14,  8, 15}, 0 },
    {  52,  48, 1, { 3,  5,  25, 3}, {24, 18, 13, 18}, 0 },
    {  29,  56, 5, { 6, 10,  23, 3}, { 5,  4,  2,  3}, 0 },
    {  35,  56, 4, { 6, 10,  23, 3}, { 9,  6,  4,  5}, 0 },
    {  42,  56, 3, { 6, 12,  21, 3}, {16,  7,  6,  9}, 0 },
    {  52,  56, 2, { 6, 10,  23, 3}, {23, 13,  8, 13}, 8 },
    {  32,  64, 5, { 6,  9,  31, 2}, { 5,  3,  2,  3}, 0 },
    {  42,  64, 4, { 6,  9,  33, 0}, {11,  6,  5,  0}, 0 },
    {  48,  64, 3, { 6, 12,  27, 3}, {16,  8,  6,  9}, 0 },
    {  58,  64, 2, { 6, 10,  29, 3}, {23, 13,  8, 13}, 8 },
    {  70,  64, 1, { 6, 11,  28, 3}, {24, 18, 12, 18}, 4 },
    {  40,  80, 5, { 6, 10,  41, 3}, { 6,  3,  2,  3}, 0 },
    {  52,  80, 4, { 6, 10,  41, 3}, {11,  6,  5,  6}, 0 },
    {  58,  80, 3, { 6, 11,  40, 3}, {16,  8,  6,  7}, 0 },
    {  70,  80, 2, { 6, 10,  41, 3}, {23, 13,  8, 13}, 8 },
    {  84,  80, 1, { 6, 10,  41, 3}, {24, 17, 12, 18}, 4 },
    {  48,  96, 5, { 7,  9,  53, 3}, { 5,  4,  2,  4}, 0 },
    {  58,  96, 4, { 7, 10,  52, 3}, { 9,  6,  4,  6}, 0 },
    {  70,  96, 3, { 6, 12,  51, 3}, {16,  9,  6, 10}, 4 },
    {  84,  96, 2, { 6, 10,  53, 3}, {22, 12,  9, 12}, 0 },
    { 104,  96, 1, { 6, 13,  50, 3}, {24, 18, 13, 19}, 0 },
    {  58, 112, 5, {14, 17,  50, 3}, { 5,  4,  2,  5}, 0 },
    {  70, 112, 4, {11, 21,  49, 3}, { 9,  6,  4,  8}, 0 },
    {  84, 112, 3, {11, 23,  47, 3}, {16,  8,  6,  9}, 0 },
    { 104, 112, 2, {11, 21,  49, 3}, {23, 12,  9, 14}, 4 },
    {  84, 128, 5, {12, 19,  62, 3}, { 5,  3,  2,  4}, 0 },
    {  64, 128, 4, {11, 21,  61, 3}, {11,  6,  5,  7}, 0 },
    {  96, 128, 3, {11, 22,  60, 3}, {16,  9,  6, 10}, 4 },
    { 116, 128, 2, {11, 21,  61, 3}, {22, 12,  9, 14}, 0 },
    { 140, 128, 1, {11, 20,  62, 3}, {24, 17, 13, 19}, 8 },
    {  80, 160, 5, {11, 19,  87, 3}, { 5,  4,  2,  4}, 0 },
    { 104, 160, 4, {11, 23,  83, 3}, {11,  6,  5,  9}, 0 },
    { 116, 160, 3, {11, 24,  82, 3}, {16,  8,  6, 11}, 0 },
    { 140, 160, 2, {11, 21,  85, 3}, {22, 11,  9, 13}, 0 },
    { 168, 160, 1, {11, 22,  84, 3}, {24, 18, 12, 19}, 0 },
    {  96, 192, 5, {11, 20, 110, 3}, { 6,  4,  2,  5}, 0 },
    { 116, 192, 4, {11, 22, 108, 3}, {10,  6,  4,  9}, 0 },
    { 140, 192, 3, {11, 24, 106, 3}, {16, 10,  6, 11}, 0 },
    { 168, 192, 2, {11, 20, 110, 3}, {22, 13,  9, 13}, 8 },
    { 208, 192, 1, {11, 21, 109, 3}, {24, 20, 13, 24}, 0 },
    { 116, 224, 5, {12, 22, 131, 3}, { 8,  6,  2,  6}, 4 },
    { 140, 224, 4, {12, 26, 127, 3}, {12,  8,  4, 11}, 0 },
    { 168, 224, 3, {11, 20, 134, 3}, {16, 10,  7,  9}, 0 },
    { 208, 224, 2, {11, 22, 132, 3}, {24, 16, 10, 15}, 0 },
    { 232, 224, 1, {11, 24, 130, 3}, {24, 20, 12, 20}, 4 },
    { 128, 256, 5, {11, 24, 154, 3}, { 6,  5,  2,  5}, 0 },
    { 168, 256, 4, {11, 24, 154, 3}, {12,  9,  5, 10}, 4 },
    { 192, 256, 3, {11, 27, 151, 3}, {16, 10,  7, 10}, 0 },
    { 232, 256, 2, {11, 22, 156, 3}, {24, 14, 10, 13}, 8 },
    { 280, 256, 1, {11, 26, 152, 3}, {24, 19, 14, 18}, 4 },
    { 160, 320, 5, {11, 26, 200, 3}, { 8,  5,  2,  6}, 4 },
    { 208, 320, 4, {11, 25, 201, 3}, {13,  9,  5, 10}, 8 },
    { 280, 320, 2, {11, 26, 200, 3}, {24, 17,  9, 17}, 0 },
    { 192, 384, 5, {11, 27, 247, 3}, { 8,  6,  2,  7}, 0 },
    { 280, 384, 3, {11, 24, 250, 3}, {16,  9,  7, 10}, 4 },
    { 416, 384, 1, {12, 28, 245, 3}, {24, 20, 14, 23}, 8 },
};

// DOC: ETSI EN 300 401
// Clause 11.3.2 - Equal Error Protection (EEP) coding 
// The number of 128bit blocks associated to a puncture code is given by a linear equation
// This takes the form of Lx = m*n+b
// Where n = An integer constants calculated from capacity_unit_multiple
struct EEP_Lx_Equation {
    int m;
    int b;
    inline int GetLx(const int n) const {
        return m*n + b;
    }
};

// EEP doesn't have a fixed subchannel size
// Each protection profile scales to the provided subchannel size in the long form
struct EEP_Descriptor {
    static constexpr int TOTAL_PUNCTURE_CODES = 2;
    // DOC: ETSI EN 300 401
    // Clause 6.2.1 - Basic sub-channel organization
    // Subchannel capacity is a multiple of a constant
    // CU = K*n, where K is provided by protection profile
    // n = An integer constant that is used for various calculations for the protection profile
    uint16_t capacity_unit_multiple;  
    EEP_Lx_Equation Lx[TOTAL_PUNCTURE_CODES];
    uint8_t PIx[TOTAL_PUNCTURE_CODES];
    // bitrate is an multiple of the integer constant n
    uint8_t bitrate_multiple;
}; 

constexpr int EEP_PROTECTION_TABLE_SIZE = 4;

// Taken from Table  9 (capacity unit multiplier) and 
//            Table 18 (puncture codes and bitrate multiple)
const EEP_Descriptor EEP_PROTECTION_TABLE_TYPE_A[EEP_PROTECTION_TABLE_SIZE] = {
    {12, {{6, -3}, {0, 3}}, {24, 23}, 8},   // 1-A
    { 8, {{2, -3}, {4, 3}}, {14, 13}, 8},   // 2-A
    { 6, {{6, -3}, {0, 3}}, { 8,  7}, 8},   // 3-A
    { 4, {{4, -3}, {2, 3}}, { 3,  2}, 8},   // 4-A
};

// EEP 2-A has a special case when n=1
const EEP_Descriptor EEP_PROT_2A_SPECIAL = 
    { 8, {{0,  5}, {0, 1}}, {13, 12}, 8};

// Taken from Table 10 (capacity unit multiplier) and 
//            Table 20 (puncture codes and bitrate multiple)
const EEP_Descriptor EEP_PROTECTION_TABLE_TYPE_B[EEP_PROTECTION_TABLE_SIZE] = {
    {27, {{24, -3}, {0, 3}}, {10,  9}, 32},   // 1-B
    {21, {{24, -3}, {0, 3}}, { 6,  5}, 32},   // 2-B
    {18, {{24, -3}, {0, 3}}, { 4,  3}, 32},   // 3-B
    {15, {{24, -3}, {0, 3}}, { 2,  1}, 32},   // 4-B
};

// DOC: ETSI EN 300 401
// Clause 11.3.2 - Equal Error Protection (EEP) coding
// Table 18 - There is a special case for EEP 2-A when the subchannel multiple constant (n) is 1
// This occurs when the subchannel has 8*n = 8 capacity units
static EEP_Descriptor GetEEPDescriptor(const Subchannel& subchannel) {
    if (subchannel.eep_type == EEP_Type::TYPE_A) {
        if (subchannel.length == 8) {
            return EEP_PROT_2A_SPECIAL;
        } else {
            return EEP_PROTECTION_TABLE_TYPE_A[subchannel.eep_prot_level];
        }
    } 
    return EEP_PROTECTION_TABLE_TYPE_B[subchannel.eep_prot_level];
}

// DOC: ETSI EN 300 401
// Clause 6.2.1 - Basic sub-channel organization
// Clause 11.3.2 - Equal Error Protection (EEP) coding
// Bitrate is calculated from the subchannel's total capacity units, and two constants
//       n = CU / k0
// bitrate = k1*n = k1/k0 * CU
static uint32_t CalculateEEPBitrate(const Subchannel& subchannel) {
    auto descriptor = GetEEPDescriptor(subchannel);
    const int n = subchannel.length / descriptor.capacity_unit_multiple;
    return n * descriptor.bitrate_multiple;
}

static UEP_Descriptor GetUEPDescriptor(const Subchannel& subchannel) {
    return UEP_PROTECTION_TABLE[subchannel.uep_prot_index];
}