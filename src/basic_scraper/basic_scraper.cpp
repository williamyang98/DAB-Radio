#include "./basic_scraper.h"
#include <stdint.h>
#include <stdio.h>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <fmt/format.h>
#include "basic_radio/basic_audio_channel.h"
#include "basic_radio/basic_audio_params.h"
#include "basic_radio/basic_dab_channel.h"
#include "basic_radio/basic_dab_plus_channel.h"
#include "basic_radio/basic_data_packet_channel.h"
#include "basic_radio/basic_radio.h"
#include "basic_radio/basic_radio.h"
#include "basic_radio/basic_slideshow.h"
#include "dab/database/dab_database.h"
#include "dab/database/dab_database_entities.h"
#include "dab/database/dab_database_types.h"
#include "utility/span.h"

namespace fs = std::filesystem;

#include "./basic_scraper_logging.h"
#define LOG_MESSAGE(...) BASIC_SCRAPER_LOG_MESSAGE(fmt::format(__VA_ARGS__))
#define LOG_ERROR(...) BASIC_SCRAPER_LOG_ERROR(fmt::format(__VA_ARGS__))

#undef GetCurrentTime // NOLINT

static std::string GetCurrentTime(void) {
    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);
    return fmt::format("{:04}-{:02}-{:02}T{:02}-{:02}-{:02}", 
        tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec);
}

static ServiceComponent* find_service_component(DAB_Database& db, subchannel_id_t id) {
    ServiceComponent* component = nullptr;
    for (auto& e: db.service_components) {
        if (e.subchannel_id == id) {
            component = &e;
            break;
        };
    }
    return component;
}

void BasicScraper::attach_to_radio(std::shared_ptr<BasicScraper> scraper, BasicRadio& radio) {
    if (scraper == nullptr) return;
    auto root_directory = scraper->m_root_directory;
    radio.On_Audio_Channel().Attach(
        [scraper, root_directory, &radio](subchannel_id_t id, Basic_Audio_Channel& channel) {
            // determine root folder
            auto& db = radio.GetDatabase();
            auto* component = find_service_component(db, id);
            if (component == nullptr) return;
            const auto service_id = component->service_reference;
            const auto component_id = component->component_id;
            const auto root_folder = fmt::format("{:s}", root_directory);
            const auto child_folder = fmt::format("service_{}_component_{}", service_id, component_id);
            auto base_path = fs::path(root_folder) / fs::path(child_folder);
            auto abs_path = fs::absolute(base_path);

            auto dab_plus_scraper = std::make_shared<Basic_Audio_Channel_Scraper>(abs_path);
            scraper->m_scrapers.push_back(dab_plus_scraper);
            Basic_Audio_Channel_Scraper::attach_to_channel(dab_plus_scraper, channel);
        }
    );
    radio.On_Data_Packet_Channel().Attach(
        [scraper, root_directory, &radio](subchannel_id_t id, Basic_Data_Packet_Channel& channel) {
            // determine root folder
            auto& db = radio.GetDatabase();
            auto* component = find_service_component(db, id);
            if (component == nullptr) return;
            const auto service_id = component->service_reference;
            const auto component_id = component->component_id;
            const auto root_folder = fmt::format("{:s}", root_directory);
            const auto child_folder = fmt::format("service_{}_component_{}", service_id, component_id);
            auto base_path = fs::path(root_folder) / fs::path(child_folder);
            auto abs_path = fs::absolute(base_path);

            auto mot_scraper = std::make_shared<BasicMOTScraper>(abs_path / "MOT");
            channel.OnMOTEntity().Attach([mot_scraper](MOT_Entity mot_entity) {
                mot_scraper->OnMOTEntity(mot_entity);
            });

            auto slideshow_scraper = std::make_shared<BasicSlideshowScraper>(abs_path / "slideshow");
            channel.GetSlideshowManager().OnNewSlideshow().Attach(
                [slideshow_scraper](std::shared_ptr<Basic_Slideshow> slideshow) {
                    slideshow_scraper->OnSlideshow(*slideshow);
                }
            );
        }
    );
}

Basic_Audio_Channel_Scraper::Basic_Audio_Channel_Scraper(const fs::path& dir) 
: m_dir(dir), 
  m_audio_scraper(dir / "audio"), 
  m_slideshow_scraper(dir / "slideshow"),
  m_mot_scraper(dir / "MOT")
{
    LOG_MESSAGE("[DAB+] Opened directory {}", m_dir.string());
}

void Basic_Audio_Channel_Scraper::attach_to_channel(std::shared_ptr<Basic_Audio_Channel_Scraper> scraper, Basic_Audio_Channel& channel) {
    if (scraper == nullptr) return;
    channel.OnAudioData().Attach(
        [scraper](BasicAudioParams params, tcb::span<const uint8_t> data) {
            scraper->m_audio_scraper.OnAudioData(params, data);
        }
    );
    channel.GetSlideshowManager().OnNewSlideshow().Attach(
        [scraper](std::shared_ptr<Basic_Slideshow> slideshow) {
            scraper->m_slideshow_scraper.OnSlideshow(*slideshow);
        }
    );
    channel.OnMOTEntity().Attach(
        [scraper](MOT_Entity mot) {
            scraper->m_mot_scraper.OnMOTEntity(mot);
        }
    );
 
    const auto ascty = channel.GetType();
    if (ascty == AudioServiceType::DAB) {
        auto& derived = dynamic_cast<Basic_DAB_Channel&>(channel);
        derived.OnMP2Data().Attach([scraper](tcb::span<const uint8_t> data) {
            auto& writer = scraper->m_audio_mp2_writer;
            if (writer == nullptr) {
                auto dir = scraper->m_dir / "mp2";
                fs::create_directories(dir);
                auto filepath = dir / fmt::format("{}_audio.mp2", GetCurrentTime());
                auto filepath_str = filepath.string();
                FILE* fp = fopen(filepath_str.c_str(), "wb+");
                if (fp != nullptr) LOG_MESSAGE("[mp2] Opened file {}", filepath_str);
                writer = std::make_unique<BasicBinaryWriter>(fp);
            }
            writer->Write(data);
        });
    } else if (ascty == AudioServiceType::DAB_PLUS) {
        auto& derived = dynamic_cast<Basic_DAB_Plus_Channel&>(channel);
        derived.OnAACData().Attach([scraper](auto superframe_header, auto mpeg4_header, auto buf) {
            auto& writer = scraper->m_audio_aac_writer;
            auto& old_header = scraper->m_old_aac_header;
            if ((writer == nullptr) || (old_header != superframe_header)) {
                auto dir = scraper->m_dir / "aac";
                fs::create_directories(dir);
                auto filepath = dir / fmt::format("{}_audio.aac", GetCurrentTime());
                auto filepath_str = filepath.string();
                FILE* fp = fopen(filepath_str.c_str(), "wb+");
                if (fp != nullptr) LOG_MESSAGE("[aac] Opened file {}", filepath_str);
                writer = std::make_unique<BasicBinaryWriter>(fp);
                old_header = superframe_header;
            }
            writer->Write(mpeg4_header);
            writer->Write(buf);
        });
    }

    auto& controls = channel.GetControls();
    controls.SetIsDecodeAudio(true);
    controls.SetIsDecodeData(true);
    controls.SetIsPlayAudio(false);
}

BasicBinaryWriter::~BasicBinaryWriter() {
    if (m_fp != nullptr) {
        fclose(m_fp);
        m_fp = nullptr;
    }
}

void BasicBinaryWriter::Write(tcb::span<const uint8_t> data) {
    if (m_fp != nullptr) {
        fwrite(data.data(), sizeof(uint8_t), data.size(), m_fp);
    }
}

BasicAudioScraper::~BasicAudioScraper() {
    if (m_fp_wav != nullptr) {
        CloseWavFile(m_fp_wav, m_total_bytes_written);
        m_fp_wav = nullptr;
        m_total_bytes_written = 0;
    }
}

void BasicAudioScraper::OnAudioData(BasicAudioParams params, tcb::span<const uint8_t> data) {
    if (!m_old_params.has_value() || (m_old_params.value() != params)) {
        if (m_fp_wav != nullptr) {
            CloseWavFile(m_fp_wav, m_total_bytes_written);
            m_fp_wav = nullptr;
            m_total_bytes_written = 0;
        }

        m_fp_wav = CreateWavFile(params);
        m_total_bytes_written = 0;
        m_old_params = std::optional(params);
    }

    if (m_fp_wav == nullptr) {
        return;
    }

    const size_t N = data.size(); 
    const size_t nb_written = fwrite(data.data(), sizeof(uint8_t), N, m_fp_wav);
    if (nb_written != N) {
        LOG_ERROR("[audio] Failed to write bytes {}/{}", nb_written, N);
    }
    m_total_bytes_written += (int)nb_written;
    UpdateWavHeader(m_fp_wav, m_total_bytes_written);
}

FILE* BasicAudioScraper::CreateWavFile(BasicAudioParams params) {
    fs::create_directories(m_dir);
    auto filepath = m_dir / fmt::format("{}_audio.wav", GetCurrentTime());
    auto filepath_str = filepath.string();

    FILE* fp = fopen(filepath_str.c_str(), "wb+");
    if (fp == nullptr) {
        LOG_ERROR("[audio] Failed to open file {}", filepath_str);
        return fp;
    }

    LOG_MESSAGE("[audio] Opened file {}", filepath_str);

    // Source: http://soundfile.sapp.org/doc/WaveFormat/
    struct WavHeader {
        char     ChunkID[4];
        int32_t  ChunkSize;
        char     Format[4];
        // Subchunk 1 = format information
        char     Subchunk1ID[4];
        int32_t  Subchunk1Size;
        int16_t  AudioFormat;
        int16_t  NumChannels;
        int32_t  SampleRate;
        int32_t  ByteRate;
        int16_t  BlockAlign;
        int16_t  BitsPerSample;
        // Subchunk 2 = data 
        char     Subchunk2ID[4];
        int32_t  Subchunk2Size;
    } header;

    const int16_t NumChannels = params.is_stereo ? 2 : 1;
    const int32_t BitsPerSample = params.bytes_per_sample * 8;
    const int32_t SampleRate = static_cast<int32_t>(params.frequency);

    std::memcpy(header.ChunkID, "RIFF", 4);
    std::memcpy(header.Format, "WAVE", 4);
    std::memcpy(header.Subchunk1ID, "fmt ", 4);
    std::memcpy(header.Subchunk2ID, "data", 4);

    header.Subchunk1Size = 16;  // size of PCM format fields 
    header.AudioFormat = 1;     // Linear quantisation
    header.NumChannels = NumChannels;     
    header.SampleRate = SampleRate;
    header.BitsPerSample = BitsPerSample;
    header.ByteRate = header.SampleRate * header.NumChannels * header.BitsPerSample / 8;
    header.BlockAlign = header.NumChannels * header.BitsPerSample / 8;

    // We update these values when we close the file
    header.Subchunk2Size = 0;
    header.ChunkSize = 36 + header.Subchunk2Size; 

    fwrite(&header, sizeof(WavHeader), 1, fp);

    return fp;
}

void BasicAudioScraper::UpdateWavHeader(FILE* fp, const int nb_data_bytes) {
    const int32_t Subchunk2Size = nb_data_bytes;
    const int32_t ChunkSize = 36 + Subchunk2Size;

    // Source: http://soundfile.sapp.org/doc/WaveFormat/
    // Refer to offset of each field
    // ChunkSize
    fseek(fp, 4, SEEK_SET);
    fwrite(&ChunkSize, sizeof(int32_t), 1, fp);
    // Subchunk2Size
    fseek(fp, 40, SEEK_SET);
    fwrite(&Subchunk2Size, sizeof(int32_t), 1, fp);

    fseek(fp, 0, SEEK_END);
}

void BasicAudioScraper::CloseWavFile(FILE* fp, const int nb_data_bytes) {
    UpdateWavHeader(fp, nb_data_bytes);
    fclose(fp);
}

void BasicSlideshowScraper::OnSlideshow(Basic_Slideshow& slideshow) {
    fs::create_directories(m_dir);
    const auto id = slideshow.transport_id;
    auto filepath = m_dir / fmt::format("{}_{}_{}", GetCurrentTime(), id, slideshow.name);
    auto filepath_str = filepath.string();

    FILE* fp = fopen(filepath_str.c_str(), "wb+");
    if (fp == nullptr) {
        LOG_ERROR("[slideshow] Failed to open file {}", filepath_str);
        return;
    }

    const auto image_buffer = slideshow.image_data;
    const size_t nb_written = fwrite(image_buffer.data(), sizeof(uint8_t), image_buffer.size(), fp);
    if (nb_written != image_buffer.size()) {
        LOG_ERROR("[slideshow] Failed to write bytes {}/{}", nb_written, image_buffer.size());
    }
    fclose(fp);

    LOG_MESSAGE("[slideshow] Wrote file {}", filepath_str);
}

void BasicMOTScraper::OnMOTEntity(MOT_Entity mot) {
    auto& content_name_str = mot.header.content_name;
    std::string content_name;
    if (content_name_str.exists) {
        content_name = std::string(content_name_str.name);
    } else {
        auto& header = mot.header;
        content_name = fmt::format("content_type_{}_{}.bin",
            header.content_type, header.content_sub_type);
    }

    fs::create_directories(m_dir);
    auto filepath = m_dir / fmt::format("{}_{}_{}", GetCurrentTime(), mot.transport_id, content_name);
    auto filepath_str = filepath.string();

    FILE* fp = fopen(filepath_str.c_str(), "wb+");
    if (fp == nullptr) {
        LOG_ERROR("[MOT] Failed to open file {}", filepath_str);
        return;
    }
    
    auto body_buf = mot.body_buf;

    const size_t nb_written = fwrite(body_buf.data(), sizeof(uint8_t), body_buf.size(), fp);
    if (nb_written != body_buf.size()) {
        LOG_ERROR("[MOT] Failed to write bytes {}/{}", nb_written, body_buf.size());
    }
    fclose(fp);

    LOG_MESSAGE("[MOT] Wrote file {}", filepath_str);
}