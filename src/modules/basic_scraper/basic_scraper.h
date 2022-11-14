#pragma once
#include "modules/basic_radio/basic_radio.h"

#include <stdio.h>
#include <stdint.h>
#include <vector>
#include <filesystem>
#include <memory>

#include "utility/span.h"
namespace fs = std::filesystem;

// Scraping output directory structure
// root
// └─service_{id}
//   └─component_{id}
//     ├─audio
//     │ └─{date}_audio.wav
//     ├─slideshow
//     │ └─{date}_{transport_id}_{label}.{ext}
//     └─MOT
//       └─{date}_{transport_id}_{label}.{ext}

class BasicAudioScraper 
{
private:
    BasicAudioParams old_params;
    FILE* fp_wav = NULL;
    int total_bytes_written = 0;
    const fs::path dir;    
public:
    BasicAudioScraper(const fs::path& _dir): dir(_dir) {
        fp_wav = NULL;
        total_bytes_written = 0;
    }
    ~BasicAudioScraper();
    BasicAudioScraper(BasicAudioScraper&) = delete;
    BasicAudioScraper(BasicAudioScraper&&) = delete;
    BasicAudioScraper& operator=(BasicAudioScraper&) = delete;
    BasicAudioScraper& operator=(BasicAudioScraper&&) = delete;
    void OnAudioData(BasicAudioParams params, tcb::span<const uint8_t> data);
private:
    FILE* CreateWavFile(BasicAudioParams params);
    void UpdateWavHeader(FILE* fp, const int nb_data_bytes);
    void CloseWavFile(FILE* fp, const int nb_data_bytes);
};

class BasicSlideshowScraper
{
private:
    const fs::path dir;
public:
    BasicSlideshowScraper(const fs::path& _dir): dir(_dir) {}
    void OnSlideshow(Basic_Slideshow& slideshow);
};

class BasicMOTScraper
{
private:
    const fs::path dir;
public:
    BasicMOTScraper(const fs::path& _dir): dir(_dir) {}
    void OnMOTEntity(MOT_Entity& mot);
};

class Basic_DAB_Plus_Scraper
{
private:
    const fs::path dir;
    BasicAudioScraper audio_scraper;
    BasicSlideshowScraper slideshow_scraper;
    BasicMOTScraper mot_scraper;
public:
    Basic_DAB_Plus_Scraper(const fs::path& _dir, Basic_DAB_Plus_Channel& channel);
    Basic_DAB_Plus_Scraper(Basic_DAB_Plus_Scraper&) = delete;
    Basic_DAB_Plus_Scraper(Basic_DAB_Plus_Scraper&&) = delete;
    Basic_DAB_Plus_Scraper& operator=(Basic_DAB_Plus_Scraper&) = delete;
    Basic_DAB_Plus_Scraper& operator=(Basic_DAB_Plus_Scraper&&) = delete;
};

class BasicScraper 
{
private:
    BasicRadio& radio;
    std::string root_directory;
    std::vector<std::unique_ptr<Basic_DAB_Plus_Scraper>> scrapers;
public:
    BasicScraper(BasicRadio& _radio, const char* _root_directory);
private:
    void Connect_DAB_Plus_Channel(subchannel_id_t id, Basic_DAB_Plus_Channel& channel);
};