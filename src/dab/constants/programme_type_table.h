#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include "../database/dab_database_types.h"

struct DAB_Programme_Label {
    std::string long_label;
    std::string short_label;
};

// DOC: ETSI TS 101 756
// Table 12: Programme type codes and abbreviations in the English language, 
//           applying to all countries, except for North America 
static const auto DAB_PROGRAMME_TYPE_TABLE_0 = std::vector<DAB_Programme_Label>{
    { "None", "None" },
    { "News", "News" },
    { "Current Affairs", "Affairs" },
    { "Information", "Info" },
    { "Sport", "Sport" },
    { "Education", "Educate" },
    { "Drama", "Drama" },
    { "Arts", "Arts" },
    { "Science", "Science" },
    { "Talk", "Talk" },
    { "Pop Music", "Pop" },
    { "Rock Music", "Rock" },
    { "Easy Listening", "Easy" },
    { "Light Classical", "Classics" },
    { "Classical Music", "Classics" },
    { "Music", "Music" },
    { "Weather", "Weather" },
    { "Finance", "Finance" },
    { "Children's", "Children" },
    { "Factual", "Factual" },
    { "Religion", "Religion" },
    { "Phone In", "Phone In" },
    { "Travel", "Travel" },
    { "Leisure", "Leisure" },
    { "Jazz and Blues", "Jazz" },
    { "Country Music", "Country" },
    { "National Music", "Nation M" },
    { "Oldies Music", "Oldies" },
    { "Folk Music", "Folk" },
    { "Documentary", "Document" },
    { "Not used", "Not used" },
    { "Not used", "Not used"  },
};

// DOC: ETSI TS 101 756
// Table 13: Programme type codes and abbreviations in the English language,
//           applying to North America 
static const auto DAB_PROGRAMME_TYPE_TABLE_1 = std::vector<DAB_Programme_Label>{
    { "None", "None" },
    { "News", "News" },
    { "Information", "Inform" },
    { "Sports", "Sports" },
    { "Talk", "Talk" },
    { "Rock", "Rock" },
    { "Classic Rock", "Cls Rock" },
    { "Adult Hits", "Adlt Hit" },
    { "Soft_Rock", "Soft_Rck" },
    { "Top 40", "Top 40" },
    { "Country", "Country" },
    { "Oldies", "Oldies" },
    { "Soft", "Soft" },
    { "Nostalgia", "Nostalga" },
    { "Jazz", "Jazz" },
    { "Classical", "Classical" },
    { "Rhythm and Blue", "R&B" },
    { "Soft Rhythm and Blues", "Soft R&B" },
    { "Foreign Language", "Language" },
    { "Religious Music", "Rel Musc" },
    { "Religious Talk", "Rel Talk" },
    { "Personality", "Persnlty" },
    { "Public", "Public" },
    { "College", "College" },
    { "RFU", "RFU" },
    { "RFU", "RFU" },
    { "RFU", "RFU" },
    { "RFU", "RFU" },
    { "RFU", "RFU" },
    { "Weather", "Weather" },
    { "Not used", "Not used" },
    { "Not used", "Not used" },
};

static const DAB_Programme_Label& GetProgrammeTypeName(uint8_t inter_table_id, programme_id_t programme_id) {
    programme_id = (programme_id & 0b11111);
    if (inter_table_id == 1) {
        return DAB_PROGRAMME_TYPE_TABLE_0[programme_id];
    } else if (inter_table_id == 2) {
        return DAB_PROGRAMME_TYPE_TABLE_1[programme_id];
    }
    static const auto DEFAULT_RETURN = DAB_Programme_Label{ "Invalid table", "Invalid table" };
    return DEFAULT_RETURN;
}