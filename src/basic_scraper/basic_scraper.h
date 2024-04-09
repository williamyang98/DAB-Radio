#pragma once
#include <stdint.h>
#include <stdio.h>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "basic_radio/basic_audio_params.h"
#include "dab/audio/aac_frame_processor.h"
#include "dab/mot/MOT_entities.h"
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
class BasicRadio;
class Basic_Audio_Channel;
struct Basic_Slideshow;

class BasicAudioScraper 
{
private:
    std::optional<BasicAudioParams> m_old_params = std::nullopt;
    FILE* m_fp_wav = nullptr;
    int m_total_bytes_written = 0;
    const fs::path m_dir;    
public:
    explicit BasicAudioScraper(const fs::path& dir): m_dir(dir) {}
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
    const fs::path m_dir;
public:
    explicit BasicSlideshowScraper(const fs::path& dir): m_dir(dir) {}
    void OnSlideshow(Basic_Slideshow& slideshow);
};

class BasicMOTScraper
{
private:
    const fs::path m_dir;
public:
    explicit BasicMOTScraper(const fs::path& dir): m_dir(dir) {}
    void OnMOTEntity(MOT_Entity mot);
};

class BasicBinaryWriter
{
private:
    FILE* m_fp = nullptr;
public:
    explicit BasicBinaryWriter(FILE* fp): m_fp(fp) {}
    ~BasicBinaryWriter();
    BasicBinaryWriter(BasicBinaryWriter&) = delete;
    BasicBinaryWriter(BasicBinaryWriter&&) = delete;
    BasicBinaryWriter& operator=(BasicBinaryWriter&) = delete;
    BasicBinaryWriter& operator=(BasicBinaryWriter&&) = delete;
    void Write(tcb::span<const uint8_t> data);
};

class Basic_Audio_Channel_Scraper
{
private:
    const fs::path m_dir;
    BasicAudioScraper m_audio_scraper;
    BasicSlideshowScraper m_slideshow_scraper;
    BasicMOTScraper m_mot_scraper;
    std::unique_ptr<BasicBinaryWriter> m_audio_aac_writer;
    std::unique_ptr<BasicBinaryWriter> m_audio_mp2_writer;
    SuperFrameHeader m_old_aac_header;
public:
    explicit Basic_Audio_Channel_Scraper(const fs::path& dir);
    static void attach_to_channel(std::shared_ptr<Basic_Audio_Channel_Scraper> scraper, Basic_Audio_Channel& channel);
};

class BasicScraper 
{
private:
    std::string m_root_directory;
    std::vector<std::shared_ptr<Basic_Audio_Channel_Scraper>> m_scrapers;
public:
    template <typename T>
    explicit BasicScraper(T root_directory): m_root_directory(root_directory) {}
    static void attach_to_radio(std::shared_ptr<BasicScraper> scraper, BasicRadio& radio);
};