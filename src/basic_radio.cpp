#include "basic_radio.h"

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

void BasicThreadedChannel::SetBuffer(uint8_t* const _buf, const int N) {
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
BasicAudioChannel::BasicAudioChannel(const Subchannel _subchannel) 
: subchannel(_subchannel) {
    msc_decoder = new MSC_Decoder(subchannel);
    aac_frame_processor = new AAC_Frame_Processor();
    pcm_player = new Win32_PCM_Player();

    const auto callback = [this](
        const int au_index, const int au_total, 
        const uint8_t* buf, const int N,
        const AAC_Decoder::Params params) 
    {
        auto pcm_params = pcm_player->GetParameters();
        pcm_params.sample_rate = params.sampling_frequency;
        pcm_params.total_channels = 2;
        pcm_params.bytes_per_sample = 2;
        pcm_player->SetParameters(pcm_params);
        pcm_player->ConsumeBuffer(buf, N);
    };

    aac_frame_processor->OnAudioFrame().Attach(callback);
}

BasicAudioChannel::~BasicAudioChannel() {
    Stop();
    Join();
    delete msc_decoder;
    delete aac_frame_processor;
    delete pcm_player;
}

void BasicAudioChannel::Run() {
    const auto* buf = GetBuffer();
    const int N = GetBufferLength();

    const int nb_cifs = 4;
    const int nb_cif_bytes = N/nb_cifs;
    for (int i = 0; i < nb_cifs; i++) {
        const auto* cif_buf = &buf[nb_cif_bytes*i];
        const int nb_decoded_bytes = msc_decoder->DecodeCIF(cif_buf, nb_cif_bytes);
        // The MSC decoder can have 0 bytes if the deinterleaver is still collecting frames
        if (nb_decoded_bytes == 0) {
            continue;
        }
        const auto* decoded_buf = msc_decoder->GetDecodedBytes();
        aac_frame_processor->Process(decoded_buf, nb_decoded_bytes);
    }
}

// Fast information channel
BasicFICRunner::BasicFICRunner() {
    misc_info = new DAB_Misc_Info();
    dab_db = new DAB_Database();
    dab_db_updater = new DAB_Database_Updater(dab_db);
    fic_decoder = new FIC_Decoder();
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

void BasicFICRunner::Run() {
    const auto* buf = GetBuffer();
    const int N = GetBufferLength();

    const int nb_fics = 4;
    const int nb_fic_bytes = N/nb_fics;
    for (int i = 0; i < nb_fics; i++) {
        const auto* fic_buf = &buf[nb_fic_bytes*i];
        fic_decoder->DecodeFIBGroup(fic_buf, i);
    }
}

BasicRadio::BasicRadio() {
    fic_runner = new BasicFICRunner();
    valid_dab_db = new DAB_Database();
}

BasicRadio::~BasicRadio() {
    channels.clear();
    delete fic_runner;
    delete valid_dab_db;
}

void BasicRadio::ProcessFrame(uint8_t* const buf, const int N) {
    const int nb_frame_length = 28800;
    const int nb_symbols = 75;
    const int nb_sym_length = nb_frame_length / nb_symbols;
    const int nb_fic_symbols = 3;
    const int nb_msc_symbols = nb_symbols - nb_fic_symbols;

    const int nb_fic_bytes = nb_fic_symbols*nb_sym_length;
    const int nb_msc_bytes = nb_msc_symbols*nb_sym_length;

    auto* fic_buf = &buf[0];
    auto* msc_buf = &buf[nb_fic_bytes];

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
        fic_runner->SetBuffer(fic_buf, nb_fic_bytes);
        for (auto& channel: selected_channels_temp) {
            channel->SetBuffer(msc_buf, nb_msc_bytes);
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
    auto lock = std::scoped_lock(mutex_db);
    db_updater->ExtractCompletedDatabase(*valid_dab_db);
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
    res = channels.insert({id, std::make_unique<BasicAudioChannel>(*subchannel)}).first;
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