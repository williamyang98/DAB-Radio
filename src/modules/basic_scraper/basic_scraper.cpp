#include "basic_scraper.h"
#include "modules/basic_radio/basic_radio.h"
#include "modules/dab/mot/MOT_processor.h"

#include <string.h>
#include <functional>
#include <ctime>
#include <fmt/core.h>

#include "easylogging++.h"
#include "fmt/core.h"

#define LOG_MESSAGE(...) CLOG(INFO, "basic-scraper") << fmt::format(__VA_ARGS__)
#define LOG_ERROR(...) CLOG(ERROR, "basic-scraper") << fmt::format(__VA_ARGS__)

#undef GetCurrentTime

std::string GetCurrentTime(void) {
    auto t = std::time(NULL);
    auto tm = *std::localtime(&t);
    return fmt::format("{:04}-{:02}-{:02}T{:02}-{:02}-{:02}", 
        tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday,
        tm.tm_hour, tm.tm_min, tm.tm_sec);
}

BasicScraper::BasicScraper(BasicRadio* _radio, const char* _root_directory) 
: radio(_radio), root_directory(_root_directory) 
{
    using namespace std::placeholders;
    radio->On_DAB_Plus_Channel().Attach(
        std::bind(&BasicScraper::Connect_DAB_Plus_Channel, this, _1, _2));
}

void BasicScraper::Connect_DAB_Plus_Channel(subchannel_id_t id, Basic_DAB_Plus_Channel& channel) {
    auto& controls = channel.GetControls();
    controls.SetIsDecodeAudio(true);
    controls.SetIsDecodeData(true);
    controls.SetIsPlayAudio(false);

    auto* db_manager = radio->GetDatabaseManager(); 

    auto lock = std::scoped_lock(db_manager->GetDatabaseMutex());
    auto* db = db_manager->GetDatabase();
    auto* component = db->GetServiceComponent_Subchannel(id);
    if (component == NULL) {
        return;
    }

    const auto service_id = component->service_reference;
    const auto component_id = component->component_id;

    const auto root_folder = fmt::format("{:s}", root_directory);
    const auto service_folder = fmt::format("service_{}", service_id);
    const auto component_folder = fmt::format("component_{}", component_id);

    auto base_path = fs::path(root_folder) / fs::path(service_folder) / fs::path(component_folder);
    auto abs_path = fs::absolute(base_path);
    scrapers.emplace_back(std::make_unique<Basic_DAB_Plus_Scraper>(abs_path, channel));
}

Basic_DAB_Plus_Scraper::Basic_DAB_Plus_Scraper(const fs::path& _dir, Basic_DAB_Plus_Channel& channel) 
: dir(_dir), 
  audio_scraper(_dir / "audio"), 
  slideshow_scraper(_dir / "slideshow"),
  mot_scraper(_dir / "MOT")
{
    LOG_MESSAGE("[DAB+] Opened directory {}", dir.string());
    using namespace std::placeholders;
    channel.OnAudioData().Attach(
        std::bind(&BasicAudioScraper::OnAudioData, &audio_scraper, _1, _2, _3));

    channel.OnSlideshow().Attach(
        std::bind(&BasicSlideshowScraper::OnSlideshow, &slideshow_scraper, _1, _2));

    channel.OnMOTEntity().Attach(
        std::bind(&BasicMOTScraper::OnMOTEntity, &mot_scraper, _1));
}

BasicAudioScraper::~BasicAudioScraper() {
    if (fp_wav != NULL) {
        CloseWavFile(fp_wav, total_bytes_written);
        fp_wav = NULL;
        total_bytes_written = 0;
    }
}

void BasicAudioScraper::OnAudioData(BasicAudioParams params, const uint8_t* data, const int N) {
    if (old_params != params) {
        if (fp_wav != NULL) {
            CloseWavFile(fp_wav, total_bytes_written);
            fp_wav = NULL;
            total_bytes_written = 0;
        }

        fp_wav = CreateWavFile(params);
        total_bytes_written = 0;
        old_params = params;
    }

    if (fp_wav == NULL) {
        return;
    }

    const int nb_written = (int)fwrite(data, sizeof(uint8_t), N, fp_wav);
    if (nb_written != N) {
        LOG_ERROR("[audio] Failed to write bytes {}/{}", nb_written, N);
    }
    total_bytes_written += nb_written;
    UpdateWavHeader(fp_wav, total_bytes_written);
}

FILE* BasicAudioScraper::CreateWavFile(BasicAudioParams params) {
    fs::create_directories(dir);
    auto filepath = dir / fmt::format("{}_audio.wav", GetCurrentTime());
    auto filepath_str = filepath.string();

    FILE* fp = fopen(filepath_str.c_str(), "wb+");
    if (fp == NULL) {
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

    strncpy(header.ChunkID, "RIFF", 4);
    strncpy(header.Format, "WAVE", 4);
    strncpy(header.Subchunk1ID, "fmt ", 4);
    strncpy(header.Subchunk2ID, "data", 4);

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

void BasicSlideshowScraper::OnSlideshow(mot_transport_id_t id, Basic_Slideshow* slideshow) {
    fs::create_directories(dir);
    auto filepath = dir / fmt::format("{}_{}_{}", GetCurrentTime(), id, slideshow->name);
    auto filepath_str = filepath.string();

    FILE* fp = fopen(filepath_str.c_str(), "wb+");
    if (fp == NULL) {
        LOG_ERROR("[slideshow] Failed to open file {}", filepath_str);
        return;
    }

    const int nb_written = (int)fwrite(slideshow->data, sizeof(uint8_t), slideshow->nb_data_bytes, fp);
    if (nb_written != slideshow->nb_data_bytes) {
        LOG_ERROR("[slideshow] Failed to write bytes {}/{}", nb_written, slideshow->nb_data_bytes);
    }
    fclose(fp);

    LOG_MESSAGE("[slideshow] Wrote file {}", filepath_str);
}

void BasicMOTScraper::OnMOTEntity(MOT_Entity* mot) {
    auto& content_name_str = mot->header.content_name;
    std::string content_name;
    if (content_name_str.exists) {
        content_name = std::string(content_name_str.name, content_name_str.nb_bytes);
    } else {
        auto& header = mot->header;
        content_name = fmt::format("content_type_{}_{}.bin",
            header.content_type, header.content_sub_type);
    }

    fs::create_directories(dir);
    auto filepath = dir / fmt::format("{}_{}_{}", GetCurrentTime(), mot->transport_id, content_name);
    auto filepath_str = filepath.string();

    FILE* fp = fopen(filepath_str.c_str(), "wb+");
    if (fp == NULL) {
        LOG_ERROR("[MOT] Failed to open file {}", filepath_str);
        return;
    }

    const int nb_written = (int)fwrite(mot->body_buf, sizeof(uint8_t), mot->nb_body_bytes, fp);
    if (nb_written != mot->nb_body_bytes) {
        LOG_ERROR("[MOT] Failed to write bytes {}/{}", nb_written, mot->nb_body_bytes);
    }
    fclose(fp);

    LOG_MESSAGE("[MOT] Wrote file {}", filepath_str);
}