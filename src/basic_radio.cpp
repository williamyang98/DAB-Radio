#include "basic_radio.h"

#include <string>
#include <stdio.h>

// TODO: make this platform independent
#include "audio/win32_pcm_player.h"

#include "easylogging++.h"
#include "fmt/core.h"

#define LOG_MESSAGE(...) CLOG(INFO, "basic-radio") << fmt::format(##__VA_ARGS__)
#define LOG_ERROR(...) CLOG(ERROR, "basic-radio") << fmt::format(##__VA_ARGS__)

// Base class for threaded channel
BasicThreadedChannel::BasicThreadedChannel() {
    buf = NULL;
    nb_bytes = 0;

    is_start = false;
    is_join = false;
    is_running = true;
    is_terminated = false;
    runner_thread = new std::thread([this]() {
        RunnerThread();
    });
}

BasicThreadedChannel::~BasicThreadedChannel() {
    Stop();
    Join();
    runner_thread->join();
    delete runner_thread;
}

void BasicThreadedChannel::SetBuffer(viterbi_bit_t* const _buf, const int N) {
    buf = _buf;
    nb_bytes = N;
}

void BasicThreadedChannel::Start() {
    auto lock = std::scoped_lock(mutex_start);
    is_start = true;
    cv_start.notify_all();
}

void BasicThreadedChannel::Join() {
    // Wait for complete termination
    if (!is_running) {
        if (is_terminated) {
            return;
        }
        
        auto lock = std::unique_lock(mutex_terminate);
        cv_terminate.wait(lock, [this]() { return is_terminated; });
        return;
    }
    auto lock = std::unique_lock(mutex_join);
    cv_join.wait(lock, [this]() { return is_join; });
    is_join = false;
}

void BasicThreadedChannel::Stop() {
    if (!is_running) {
        return;
    }
    is_running = false;
    Start();
}

void BasicThreadedChannel::RunnerThread() {
    BeforeRun();
    while (is_running) {
        {
            auto lock = std::unique_lock(mutex_start);
            cv_start.wait(lock, [this]() { return is_start; });
            is_start = false;
        }
        if (!is_running) {
            auto lock = std::scoped_lock(mutex_join);
            is_join = true;
            cv_join.notify_all();
            break;
        }
        Run();
        {
            auto lock = std::scoped_lock(mutex_join);
            is_join = true;
            cv_join.notify_all();
        }
    }

    auto lock = std::scoped_lock(mutex_terminate);
    is_terminated = true;
    cv_terminate.notify_all();
}

// Audio channel on the MSC
BasicAudioChannel::BasicAudioChannel(const DAB_Parameters _params, const Subchannel _subchannel) 
: params(_params), subchannel(_subchannel) {
    msc_decoder = new MSC_Decoder(subchannel);
    aac_frame_processor = new AAC_Frame_Processor();
    aac_audio_decoder = NULL;
    aac_data_decoder = new AAC_Data_Decoder();
    slideshow_processor = new MOT_Slideshow_Processor();

    pcm_player = new Win32_PCM_Player();

    aac_frame_processor->OnSuperFrameHeader().Attach([this](SuperFrameHeader header) {
        AAC_Audio_Decoder::Params audio_params;
        audio_params.sampling_frequency = header.sampling_rate;
        audio_params.is_PS = header.PS_flag;
        audio_params.is_SBR = header.SBR_flag;
        audio_params.is_stereo = header.is_stereo;

        if (aac_audio_decoder == NULL) {
            aac_audio_decoder = new AAC_Audio_Decoder(audio_params);
            return;
        }

        const auto old_audio_params = aac_audio_decoder->GetParams();
        if (old_audio_params != audio_params) {
            delete aac_audio_decoder;
            aac_audio_decoder = new AAC_Audio_Decoder(audio_params);
        }
    });

    aac_frame_processor->OnAccessUnit().Attach([this](const int au_index, const int nb_aus, uint8_t* buf, const int N) {
        if (aac_audio_decoder == NULL) {
            return;
        }
        const auto res = aac_audio_decoder->DecodeFrame(buf, N);
        if (res.is_error) {
            LOG_ERROR("[basic-audio-channel] [aac-audio-decoder] error={} au_index={}/{}", 
                res.error_code, au_index, nb_aus);
            return;
        }

        return;
        const auto audio_params = aac_audio_decoder->GetParams();
        auto pcm_params = pcm_player->GetParameters();
        pcm_params.sample_rate = audio_params.sampling_frequency;
        pcm_params.total_channels = 2;
        pcm_params.bytes_per_sample = 2;
        pcm_player->SetParameters(pcm_params);
        pcm_player->ConsumeBuffer(res.audio_buf, res.nb_audio_buf_bytes);
    });

    aac_frame_processor->OnAccessUnit().Attach([this](const int au_index, const int nb_aus, uint8_t* buf, const int N) {
        aac_data_decoder->ProcessAccessUnit(buf, N);
    });

    auto& pad_processor = aac_data_decoder->Get_PAD_Processor();
    pad_processor.OnLabelUpdate().Attach([this](const uint8_t* label, const int N) {
        const auto label_str = std::string(reinterpret_cast<const char*>(label), N);
        LOG_MESSAGE("dynamic_label[{}] = {}", N, label_str);
    });

    pad_processor.OnMOTUpdate().Attach([this](MOT_Entity entity) {
        // DOC: ETSI TS 101 756
        // Table 17: Content type and content subtypes 

        // DOC: ETSI TS 101 499
        // Clause 6.2.3 MOT ContentTypes and ContentSubTypes 
        // For specific types used for slideshows

        const auto type = entity.header.content_type;
        const auto sub_type = entity.header.content_sub_type;
        // Content type: Image
        if (type != 0b000010) {
            return;
        }
        // Content subtype: JPEG and PNG
        if ((sub_type != 0b01) && (sub_type != 0b11)) {
            return;
        }

        const bool is_jpg = (sub_type == 0b01);

        // User application header extension parameters
        MOT_Slideshow slideshow_data;
        for (auto& p: entity.header.user_app_params) {
            slideshow_processor->ProcessHeaderExtension(
                &slideshow_data, 
                p.type, p.data, p.nb_data_bytes);
        }

        static char filename[256] = {0};
        if (entity.header.content_name.exists) {
            snprintf(filename, sizeof(filename), "images/subchannel_%d_%.*s", 
                subchannel.id,
                entity.header.content_name.nb_bytes,
                entity.header.content_name.name);
        } else {
            snprintf(filename, sizeof(filename), "images/subchannel_%d_tid_%d.%s", 
                subchannel.id, entity.transport_id, is_jpg ? "jpg" : "png");
        } 

        // FILE* fp = fopen(filename, "wb+");
        // if (fp == NULL) {
        //     LOG_ERROR("Failed to write slideshow {}", filename);
        //     return;
        // }
        // fwrite(entity.body_buf, sizeof(uint8_t), entity.nb_body_bytes, fp);
        // fclose(fp);

        LOG_MESSAGE("Wrote image to {}", filename);
    });
}

BasicAudioChannel::~BasicAudioChannel() {
    Stop();
    Join();
    delete msc_decoder;
    delete aac_frame_processor;
    if (aac_audio_decoder != NULL) {
        delete aac_audio_decoder;
    }
    delete aac_data_decoder;
    delete slideshow_processor;
    delete pcm_player;
}

void BasicAudioChannel::BeforeRun() {
    el::Helpers::setThreadName(fmt::format("MSC-subchannel-{}", subchannel.id));
}

void BasicAudioChannel::Run() {
    const auto* buf = GetBuffer();
    const int N = GetBufferLength();

    if (N != params.nb_msc_bits) {
        LOG_ERROR("[basic-audio-channel] Got incorrect number of MSC bits {}/{}",
            N, params.nb_msc_bits);
        return;
    }

    for (int i = 0; i < params.nb_cifs; i++) {
        const auto* cif_buf = &buf[params.nb_cif_bits*i];
        const int nb_decoded_bytes = msc_decoder->DecodeCIF(cif_buf, params.nb_cif_bits);
        // The MSC decoder can have 0 bytes if the deinterleaver is still collecting frames
        if (nb_decoded_bytes == 0) {
            continue;
        }
        const auto* decoded_buf = msc_decoder->GetDecodedBytes();
        aac_frame_processor->Process(decoded_buf, nb_decoded_bytes);
    }
}

// Fast information channel
BasicFICRunner::BasicFICRunner(const DAB_Parameters _params) 
: params(_params)
{
    misc_info = new DAB_Misc_Info();
    dab_db = new DAB_Database();
    dab_db_updater = new DAB_Database_Updater(dab_db);
    fic_decoder = new FIC_Decoder(params.nb_fib_cif_bits);
    fig_processor = new FIG_Processor();
    fig_handler = new Radio_FIG_Handler();

    fig_handler->SetUpdater(dab_db_updater);
    fig_handler->SetMiscInfo(misc_info);
    fig_processor->SetHandler(fig_handler);
    fic_decoder->OnFIB().Attach([this] 
    (const uint8_t* buf, const int N) 
    {
        fig_processor->ProcessFIG(buf);
    });
}

BasicFICRunner::~BasicFICRunner() {
    Stop();
    Join();
    delete misc_info;
    delete dab_db;
    delete dab_db_updater;
    delete fic_decoder;
    delete fig_processor;
    delete fig_handler;
}

void BasicFICRunner::BeforeRun() {
    el::Helpers::setThreadName("FIC");
}

void BasicFICRunner::Run() {
    const auto* buf = GetBuffer();
    const int N = GetBufferLength();

    if (N != params.nb_fic_bits) {
        LOG_ERROR("[fic-runner] Got incorrect number of bits in fic {]/{}",
            N, params.nb_fic_bits);
        return;
    }

    for (int i = 0; i < params.nb_cifs; i++) {
        const auto* fib_cif_buf = &buf[params.nb_fib_cif_bits*i];
        fic_decoder->DecodeFIBGroup(fib_cif_buf, i);
    }
}

BasicRadio::BasicRadio(const DAB_Parameters _params)
: params(_params) 
{
    fic_runner = new BasicFICRunner(params);
    valid_dab_db = new DAB_Database();
}

BasicRadio::~BasicRadio() {
    channels.clear();
    delete fic_runner;
    delete valid_dab_db;
}

void BasicRadio::Process(viterbi_bit_t* const buf, const int N) {
    if (N != params.nb_frame_bits) {
        LOG_ERROR("Got incorrect number of frame bits {}/{}", N, params.nb_frame_bits);
        return;
    }

    auto* fic_buf = &buf[0];
    auto* msc_buf = &buf[params.nb_fic_bits];
    {
        auto lock = std::scoped_lock(mutex_channels);
        selected_channels_temp.clear();
        for (auto& [_, channel]: channels) {
            if (channel->is_selected) {
                selected_channels_temp.push_back(channel.get());
            }
        }
    }

    {
        fic_runner->SetBuffer(fic_buf, params.nb_fic_bits);
        for (auto& channel: selected_channels_temp) {
            channel->SetBuffer(msc_buf, params.nb_msc_bits);
        }

        // Launch all channel threads
        fic_runner->Start();
        for (auto& channel: selected_channels_temp) {
            channel->Start();
        }

        // Join them all now
        fic_runner->Join();
        for (auto& channel: selected_channels_temp) {
            channel->Join();
        }
    }

    UpdateDatabase();
}

void BasicRadio::UpdateDatabase() {
    misc_info = *(fic_runner->GetMiscInfo());
    auto* live_db = fic_runner->GetLiveDatabase();
    auto* db_updater = fic_runner->GetDatabaseUpdater();
    
    auto curr_stats = db_updater->GetStatistics();
    const bool is_changed = (previous_stats != curr_stats);
    previous_stats = curr_stats;

    // If there is a change, wait for changes to stabilise
    if (is_changed) {
        is_awaiting_db_update = true;
        nb_cooldown = 0;
        return;
    }

    // If we know the databases are desynced update cooldown
    if (is_awaiting_db_update) {
        nb_cooldown++;
        LOG_MESSAGE("cooldown={}/{}", nb_cooldown, nb_cooldown_max);
    }

    if (nb_cooldown != nb_cooldown_max) {
        return;
    }

    is_awaiting_db_update = false;
    nb_cooldown = 0;
    LOG_MESSAGE("Updating internal database");

    // If the cooldown has been reached, then we consider
    // the databases to be sufficiently stable to copy
    // This is an expensive operation so we should only do it when there are few changes
    {
        auto lock = std::scoped_lock(mutex_db);
        db_updater->ExtractCompletedDatabase(*valid_dab_db);
    }

    // TODO: Auto run subchannels if we are in data scrapeing mode
    for (auto& subchannel: valid_dab_db->subchannels) {
        const auto id = subchannel.id;
        if (IsSubchannelAdded(id)) continue;
        AddSubchannel(id);
    }
}

void BasicRadio::AddSubchannel(const subchannel_id_t id) {
    // NOTE: We expect the caller to have this mutex held
    // auto lock = std::scoped_lock(mutex_channels);
    auto res = channels.find(id);
    if (res != channels.end()) {
        // LOG_ERROR("Selected subchannel {} already has an instance running", id);
        auto& v = res->second->is_selected;
        v = !v;
        return;
    }

    auto* db = valid_dab_db;
    auto* subchannel = db->GetSubchannel(id);
    if (subchannel == NULL) {
        LOG_ERROR("Selected subchannel {} which doesn't exist in db", id);
        return;
    }

    auto* service_component = db->GetServiceComponent_Subchannel(id);
    if (service_component == NULL) {
        LOG_ERROR("Selected subchannel {} has no service component", id);
        return;
    }

    const auto mode = service_component->transport_mode;
    if (mode != TransportMode::STREAM_MODE_AUDIO) {
        LOG_ERROR("Selected subchannel {} which isn't an audio stream", id);
        return;
    }

    const auto ascty = service_component->audio_service_type;
    if (ascty != AudioServiceType::DAB_PLUS) {
        LOG_ERROR("Selected subchannel {} isn't a DAB+ stream", id);
        return;
    }

    // create our instance
    LOG_MESSAGE("Added subchannel {}", id);
    res = channels.insert({id, std::make_unique<BasicAudioChannel>(params, *subchannel)}).first;
    res->second->is_selected = true;
}

bool BasicRadio::IsSubchannelAdded(const subchannel_id_t id) {
    // NOTE: This would be extremely slow with alot of subchannels
    // auto lock = std::scoped_lock(mutex_channels);
    auto res = channels.find(id);
    if (res == channels.end()) {
        return false;
    }
    return res->second->is_selected;
}
