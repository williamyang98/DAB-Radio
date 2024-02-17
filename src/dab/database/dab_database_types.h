#pragma once
#include <stdint.h>

// DOC: ETSI EN 300 401
// The size of these fields were derived from the length of the bitfields that carry them
// These are found in Clause 5.2 to Clause 8 

typedef uint16_t ensemble_id_t;             // 12bits
typedef uint32_t service_id_t;              // 12 to 20bits
typedef uint8_t  service_component_id_t;    // 4bits
typedef uint16_t service_component_global_id_t; // 12bits
typedef uint8_t  subchannel_id_t;           // 6bits

typedef uint8_t  country_id_t;              // 4bits
typedef uint8_t  extended_country_id_t;     // 8bits
typedef uint8_t  language_id_t;             // 8bits
typedef uint8_t  programme_id_t;            // 5bits
typedef uint8_t  closed_caption_id_t;       // 8bits

typedef uint16_t subchannel_addr_t;         // 10bits
typedef uint16_t subchannel_size_t;         // 10bits (in capacity units)
typedef uint8_t  eep_protection_level_t;    // 2bits (EEP) table index
typedef uint8_t  uep_protection_index_t;    // 6bits (UEP) table index

typedef uint16_t lsn_t;                     // 12bits (linkage set number)
typedef uint16_t fm_id_t;                   // 16bits
typedef uint32_t drm_id_t;                  // 24bits
typedef uint32_t amss_id_t;                 // 24bits

typedef uint32_t freq_t;                    // 32bit unsigned integer (Hz)
