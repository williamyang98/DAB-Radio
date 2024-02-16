#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <argparse/argparse.hpp>
#include <fmt/core.h>
#include "basic_scraper/basic_scraper.h"
#include "./block_frequencies.h"
#include "./app_helpers/app_io_buffers.h"
#include "./app_helpers/app_ofdm_blocks.h"
#include "./app_helpers/app_radio_blocks.h"
#include "./app_helpers/app_viterbi_convert_block.h"
#include "./app_helpers/app_audio.h"
#include "./app_helpers/app_common_gui.h"
#include "./app_helpers/app_logging.h"
#include "./audio/audio_pipeline.h"
#include "./audio/portaudio_sink.h"
#include "./device/device.h"
#include "./device/device_list.h"
#include "./gui/ofdm/render_ofdm_demod.h"
#include "./gui/ofdm/render_profiler.h"
#include "./gui/basic_radio/render_basic_radio.h"
#include "./gui/basic_radio/basic_radio_view_controller.h"
#include "./gui/audio/render_portaudio_controls.h"
#include "./gui/device/render_devices.h"

void init_parser(argparse::ArgumentParser& parser) {
    parser.add_argument("--input")
        .default_value(std::string(""))
        .metavar("INPUT_FILENAME")
        .nargs(1).required()
        .help("Filename of input to radio (defaults to stdin)");
    parser.add_argument("--transmission-mode")
        .default_value(int(1)).scan<'i', int>()
        .choices(1,2,3,4)
        .metavar("MODE")
        .nargs(1).required()
        .help("Dab transmission mode");
    parser.add_argument("--tuner-default-channel")
        .default_value(std::string("9C"))
        .metavar("CHANNEL")
        .nargs(1).required()
        .help("Tuner will automatically switch to this channel on startup");
    parser.add_argument("--tuner-manual-gain")
        .default_value(19.0f).scan<'g', float>()
        .metavar("GAIN")
        .nargs(1).required()
        .help("Tuner will use this gain on startup");
    parser.add_argument("--tuner-auto-gain")
        .default_value(false).implicit_value(true)
        .help("Tuner will use auto gain instead of manual gain");
    parser.add_argument("--tuner-device-index")
        .default_value(size_t(0)).scan<'u', size_t>()
        .metavar("DEVICE_INDEX")
        .nargs(1).required()
        .help("Index of tuner to select from list automatically");
    parser.add_argument("--tuner-no-auto-select")
        .default_value(false).implicit_value(true)
        .help("Do not automatically select tuner on startup");
    parser.add_argument("--ofdm-block-size")
        .default_value(size_t(65536)).scan<'u', size_t>()
        .metavar("BLOCK_SIZE")
        .nargs(1).required()
        .help("Number of bytes the OFDM demodulator will read in each block");
    parser.add_argument("--ofdm-total-threads")
        .default_value(size_t(1)).scan<'u', size_t>()
        .metavar("TOTAL_THREADS")
        .nargs(1).required()
        .help("Number of OFDM demodulator threads (0 = max number of threads)");
    parser.add_argument("--ofdm-disable-coarse-freq")
        .default_value(false).implicit_value(true)
        .help("Disable OFDM coarse frequency correction");
    parser.add_argument("--radio-total-threads")
        .default_value(size_t(1)).scan<'u', size_t>()
        .metavar("TOTAL_THREADS")
        .nargs(1).required()
        .help("Number of basic radio threads (0 = max number of threads)");
    parser.add_argument("--radio-enable-logging")
        .default_value(false).implicit_value(true)
        .help("Enable verbose logging for radio");
    parser.add_argument("--scraper-enable")
        .default_value(false).implicit_value(true)
        .help("Radio scraper will be used to save radio data to a directory");
    parser.add_argument("--scraper-output")
        .default_value(std::string("data/scraper_tuner"))
        .metavar("OUTPUT_FOLDER")
        .nargs(1).required()
        .help("Output folder for scraper");
    parser.add_argument("--scraper-disable-logging")
        .default_value(false).implicit_value(true)
        .help("Disable verbose logging for scraper");
    parser.add_argument("--scraper-disable-auto")
        .default_value(false).implicit_value(true)
        .help("Disable automatic scraping of new channels");
    parser.add_argument("--audio-no-auto-select")
        .default_value(false).implicit_value(true)
        .help("Disable automatic selection of output audio device");
    parser.add_argument("--list-channels")
        .default_value(false).implicit_value(true)
        .help("List all DAB channels");
}

struct Args {
    std::string input_file; 
    int transmission_mode;
    std::string tuner_default_channel;
    float tuner_manual_gain;
    bool tuner_auto_gain;
    size_t tuner_device_index;
    bool tuner_no_auto_select;
    size_t ofdm_block_size;
    size_t ofdm_total_threads;
    bool ofdm_disable_coarse_freq;
    size_t radio_total_threads;
    bool radio_enable_logging;
    bool scraper_enable;
    std::string scraper_output;
    bool scraper_disable_logging;
    bool scraper_disable_auto;
    bool audio_no_auto_select;
    bool is_list_channels;
};

Args get_args_from_parser(const argparse::ArgumentParser& parser) {
    Args args;
    args.input_file = parser.get<std::string>("--input");
    args.transmission_mode = parser.get<int>("--transmission-mode");
    args.tuner_default_channel = parser.get<std::string>("--tuner-default-channel");
    args.tuner_manual_gain = parser.get<float>("--tuner-manual-gain");
    args.tuner_auto_gain = parser.get<bool>("--tuner-auto-gain");
    args.tuner_device_index = parser.get<size_t>("--tuner-device-index");
    args.tuner_no_auto_select = parser.get<bool>("--tuner-no-auto-select");
    args.ofdm_block_size = parser.get<size_t>("--ofdm-block-size");
    args.ofdm_total_threads = parser.get<size_t>("--ofdm-total-threads");
    args.ofdm_disable_coarse_freq = parser.get<bool>("--ofdm-disable-coarse-freq");
    args.radio_total_threads = parser.get<size_t>("--radio-total-threads");
    args.radio_enable_logging = parser.get<bool>("--radio-enable-logging");
    args.scraper_enable = parser.get<bool>("--scraper-enable");
    args.scraper_output = parser.get<std::string>("--scraper-output");
    args.scraper_disable_logging = parser.get<bool>("--scraper-disable-logging");
    args.scraper_disable_auto = parser.get<bool>("--scraper-disable-auto");
    args.audio_no_auto_select = parser.get<bool>("--audio-no-auto-select");
    args.is_list_channels = parser.get<bool>("--list-channels");
    return args;
}

class Radio_Instance {
private:
    const std::string m_name;
    BasicRadio m_radio;
    BasicRadioViewController m_view_controller;
public:
    template <typename... T>
    Radio_Instance(std::string_view name, T... args): m_name(name), m_radio(std::forward<T>(args)...) {}
    auto& get_radio() { return m_radio; }
    auto& get_view_controller() { return m_view_controller; }
    std::string_view get_name() const { return m_name; }
};

class Basic_Radio_Switcher 
{
private:
    DAB_Parameters m_dab_params;
    std::shared_ptr<InputBuffer<viterbi_bit_t>> m_input_stream = nullptr;
    std::vector<viterbi_bit_t> m_bits_buffer;
    std::map<std::string, std::shared_ptr<Radio_Instance>> m_instances;
    std::shared_ptr<Radio_Instance> m_selected_instance = nullptr;
    std::mutex m_mutex_selected_instance;
    size_t m_flush_reads = 0;
    std::function<std::shared_ptr<Radio_Instance>(const DAB_Parameters&,std::string_view)> m_create_instance;
public:
    template <typename F>
    Basic_Radio_Switcher(int transmission_mode, F&& create_instance)
    : m_dab_params(get_dab_parameters(transmission_mode)),
      m_create_instance(create_instance)
    {
        m_bits_buffer.resize(m_dab_params.nb_frame_bits);
    }
    void set_input_stream(std::shared_ptr<InputBuffer<viterbi_bit_t>> stream) { 
        m_input_stream = stream; 
    }
    void flush_input_stream() {
        m_flush_reads = 5;
    }
    void switch_instance(std::string_view key) {
        auto lock = std::unique_lock(m_mutex_selected_instance);
        auto res = m_instances.find(std::string(key));
        std::shared_ptr<Radio_Instance> new_instance = nullptr;
        if (res != m_instances.end()) {
            new_instance = res->second;
        } else {
            new_instance = m_create_instance(m_dab_params, key);
            m_instances.insert({ std::string(key), new_instance });
        }
        if (m_selected_instance != new_instance) {
            flush_input_stream();
        }
        m_selected_instance = new_instance;
    }
    std::shared_ptr<Radio_Instance> get_instance() {
        auto lock = std::unique_lock(m_mutex_selected_instance);
        return m_selected_instance;
    }
    void run() {
        if (m_input_stream == nullptr) return;
        while (true) {
            const size_t length = m_input_stream->read(m_bits_buffer);
            if (length != m_bits_buffer.size()) return;

            auto lock = std::unique_lock(m_mutex_selected_instance);
            if (m_flush_reads > 0) {
                m_flush_reads -= 1;
                continue;
            }
            if (m_selected_instance == nullptr) continue;
            m_selected_instance->get_radio().Process(m_bits_buffer);
        }
    }
};

class DeviceSource {
private:
    std::shared_ptr<Device> m_device = nullptr;
    std::mutex m_mutex_device;
    std::function<void(std::shared_ptr<Device>)> m_device_change_callback;
public:
    template <typename F>
    DeviceSource(F&& device_change_callback)
    : m_device_change_callback(device_change_callback) 
    {}
    std::shared_ptr<Device> get_device() { 
        auto lock = std::unique_lock(m_mutex_device);
        return m_device;
    }
    void set_device(std::shared_ptr<Device> device) {
        auto lock = std::unique_lock(m_mutex_device);
        m_device = device;
        m_device_change_callback(m_device);
    }
};

static void list_channels() {
    struct Channel {
        const char *name;
        uint32_t frequency_Hz;
    };
    // Sort by frequency
    std::vector<Channel> channels;
    for (const auto& [channel, frequency_Hz]: block_frequencies) {
        channels.push_back({ channel.c_str(), frequency_Hz });
    }
    std::sort(channels.begin(), channels.end(), [](const auto& a, const auto& b) {
        return a.frequency_Hz < b.frequency_Hz;
    });
    fprintf(stderr, "Block |    Frequency\n");
    for (const auto& channel: channels) {
        const float frequency_MHz = float(channel.frequency_Hz) * 1e-6f;
        fprintf(stderr, "%*s | %8.3f MHz\n", 5, channel.name, frequency_MHz);
    }
}

INITIALIZE_EASYLOGGINGPP
int main(int argc, char** argv) {
    const char* PROGRAM_NAME = "radio_app";
    const char* PROGRAM_DESCRIPTION = "Radio app that connects to tuner";
    const char* PROGRAM_VERSION_NAME = "0.1.0";
    auto parser = argparse::ArgumentParser(PROGRAM_NAME, PROGRAM_VERSION_NAME);
    parser.add_description(PROGRAM_DESCRIPTION);
    init_parser(parser);
    try {
        parser.parse_args(argc, argv);
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << std::endl;
        std::cerr << parser;
        return 1;
    }
    const auto args = get_args_from_parser(parser);

    if (args.is_list_channels) {
        fprintf(stderr, "Valid DAB channels are:\n");
        list_channels();
        return 1;
    }

    if (args.ofdm_block_size == 0) {
        fprintf(stderr, "OFDM block size cannot be zero\n");
        return 1;
    }

    const auto tuner_default_channel = args.tuner_default_channel;
    if (block_frequencies.find(tuner_default_channel) == block_frequencies.end()) {
        fprintf(stderr, "Invalid channel block '%s'. Refer to --list-channels for valid blocks\n", tuner_default_channel.c_str());
        list_channels();
        return 1;
    }

    setup_easylogging(false, args.radio_enable_logging, !args.scraper_disable_logging); 

    const auto dab_params = get_dab_parameters(args.transmission_mode);
    // ofdm
    auto ofdm_block = std::make_shared<OFDM_Block>(args.transmission_mode, args.ofdm_total_threads);
    auto& ofdm_config = ofdm_block->get_ofdm_demod().GetConfig();
    ofdm_config.sync.is_coarse_freq_correction = !args.ofdm_disable_coarse_freq;
    // radio switcher
    auto audio_pipeline = std::make_shared<AudioPipeline>();
    auto radio_switcher = std::make_shared<Basic_Radio_Switcher>(
        args.transmission_mode,
        [args, audio_pipeline](const DAB_Parameters& params, std::string_view channel_name) -> auto {
            auto instance = std::make_shared<Radio_Instance>(channel_name, params, args.radio_total_threads);
            auto& radio = instance->get_radio(); 
            attach_audio_pipeline_to_radio(audio_pipeline, radio);
            if (args.scraper_enable) {
                auto dir = fmt::format("{}/{}", args.scraper_output, channel_name);
                auto scraper = std::make_shared<BasicScraper>(dir);
                fprintf(stderr, "basic_scraper is writing to folder '%s'\n", dir.c_str()); 
                BasicScraper::attach_to_radio(scraper, radio);
                if (!args.scraper_disable_auto) {
                    radio.On_Audio_Channel().Attach(
                        [](subchannel_id_t subchannel_id, Basic_Audio_Channel& channel) {
                            auto& controls = channel.GetControls();
                            controls.SetIsDecodeAudio(true);
                            controls.SetIsDecodeData(true);
                            controls.SetIsPlayAudio(false);
                        }
                    );
                }
            }
            return instance;
        }
    );
    // ofdm input
    auto device_output_buffer = std::make_shared<ThreadedRingBuffer<RawIQ>>(args.ofdm_block_size*sizeof(RawIQ));
    auto ofdm_convert_raw_iq = std::make_shared<OFDM_Convert_RawIQ>();
    ofdm_convert_raw_iq->set_input_stream(device_output_buffer);
    ofdm_block->set_input_stream(ofdm_convert_raw_iq);
    // connect ofdm to radio_switcher
    auto ofdm_to_radio_buffer = std::make_shared<ThreadedRingBuffer<viterbi_bit_t>>(dab_params.nb_frame_bits*2);
    ofdm_block->set_output_stream(ofdm_to_radio_buffer);
    radio_switcher->set_input_stream(ofdm_to_radio_buffer);
    // device to ofdm
    auto device_list = std::make_shared<DeviceList>();
    auto device_source = std::make_shared<DeviceSource>(
        [device_output_buffer, radio_switcher, args]
        (std::shared_ptr<Device> device) {
            radio_switcher->flush_input_stream();
            if (device == nullptr) return;
            if (args.tuner_auto_gain) {
                device->SetAutoGain();
            } else {
                device->SetNearestGain(args.tuner_manual_gain);
            }
            device->SetDataCallback([device_output_buffer](tcb::span<const uint8_t> bytes) {
                constexpr size_t BYTES_PER_SAMPLE = sizeof(RawIQ);
                const size_t total_bytes = bytes.size() - (bytes.size() % BYTES_PER_SAMPLE);
                const size_t total_samples = total_bytes / BYTES_PER_SAMPLE;
                auto raw_iq = tcb::span(
                    reinterpret_cast<const RawIQ*>(bytes.data()),
                    total_samples
                );
                const size_t total_read_samples = device_output_buffer->write(raw_iq);
                const size_t total_read_bytes = total_read_samples * BYTES_PER_SAMPLE;
                return total_read_bytes;
            });
            device->SetFrequencyChangeCallback([radio_switcher](const std::string& label, const uint32_t freq) {
                radio_switcher->switch_instance(label);
            });
            device->SetCenterFrequency(args.tuner_default_channel, block_frequencies.at(args.tuner_default_channel));
        }
    ); 
    // audio
    auto portaudio_global_handler = std::make_unique<PortAudioGlobalHandler>();
    auto portaudio_threaded_actions = std::make_shared<PortAudioThreadedActions>();
    // gui
    CommonGui gui;
    gui.window_title = "Radio App";
    gui.render_callback = [ofdm_block, radio_switcher, portaudio_threaded_actions, audio_pipeline, device_source, device_list] () {
        if (ImGui::Begin("OFDM Demodulator")) {
            ImGuiID dockspace_id = ImGui::GetID("Demodulator Dockspace");
            ImGui::DockSpace(dockspace_id);
            RenderSourceBuffer(ofdm_block->get_buffer());
            RenderOFDMDemodulator(ofdm_block->get_ofdm_demod());
            RenderProfiler();
            if (ImGui::Begin("Tuner Controls")) {
                auto device = device_source->get_device();
                auto selected_device = RenderDeviceList(*(device_list.get()), device.get());
                if (device != nullptr) {
                    RenderDevice(*(device.get()), block_frequencies);
                }
                if (selected_device != nullptr) {
                    device_source->set_device(selected_device);
                }
            }
            ImGui::End();
        }
        ImGui::End();

        auto instance = radio_switcher->get_instance();
        if (instance != nullptr) {
            auto window_label = fmt::format("Simple View ({})###simple_view", instance->get_name()); 
            if (ImGui::Begin(window_label.c_str())) {
                ImGuiID dockspace_id = ImGui::GetID("Simple View Dockspace");
                ImGui::DockSpace(dockspace_id);
                if (ImGui::Begin("Audio Controls")) {
                    RenderPortAudioControls(*(portaudio_threaded_actions.get()), audio_pipeline);
                    RenderVolumeSlider(audio_pipeline->get_global_gain());
                }
                ImGui::End();
                    auto& radio = instance->get_radio();
                    auto& view_controller = instance->get_view_controller();
                    RenderBasicRadio(radio, view_controller);
            }
            ImGui::End();
        }
    };
    // threads
    std::unique_ptr<std::thread> thread_select_default_audio = nullptr;
    if (!args.audio_no_auto_select) {
        thread_select_default_audio = std::make_unique<std::thread>([portaudio_threaded_actions, audio_pipeline]() {
            const PaDeviceIndex device_index = get_default_portaudio_device_index();
            portaudio_threaded_actions->select_device(device_index, audio_pipeline); 
        });
    }
    std::unique_ptr<std::thread> thread_select_default_tuner = nullptr;
    if (!args.tuner_no_auto_select) {
        const size_t default_device_index = args.tuner_device_index;
        thread_select_default_tuner = std::make_unique<std::thread>([device_list, device_source, default_device_index]() {
            device_list->refresh();
            size_t total_descriptors = 0;
            {
                auto lock = std::unique_lock(device_list->get_mutex_descriptors());
                auto descriptors = device_list->get_descriptors();
                total_descriptors = descriptors.size();
            }
            if (default_device_index >= total_descriptors) {
                fprintf(stderr, "ERROR: Device index is greater than the number of devices (%zu >= %zu)\n", default_device_index, total_descriptors);
                return;
            }
            auto device = device_list->get_device(default_device_index);
            if (device == nullptr) return;
            device_source->set_device(device);
        });
    }
    const size_t ofdm_block_size = args.ofdm_block_size;
    auto thread_ofdm_run = std::thread([ofdm_block, ofdm_block_size, ofdm_to_radio_buffer]() {
        ofdm_block->run(ofdm_block_size);
        fprintf(stderr, "ofdm thread finished\n");
        if (ofdm_to_radio_buffer != nullptr) ofdm_to_radio_buffer->close();
    });
    auto thread_radio_switcher = std::thread([radio_switcher]() {
        radio_switcher->run();
        fprintf(stderr, "radio_switcher thread finished\n");
    });
    // shutdown
    const int gui_retval = render_common_gui_blocking(gui);
    device_output_buffer->close();
    ofdm_to_radio_buffer->close();
    if (thread_select_default_audio != nullptr) thread_select_default_audio->join();
    if (thread_select_default_tuner != nullptr) thread_select_default_tuner->join();
    thread_ofdm_run.join();
    thread_radio_switcher.join();
    ofdm_block = nullptr;
    radio_switcher = nullptr;
    portaudio_threaded_actions = nullptr;
    audio_pipeline = nullptr;
    // NOTE: need to shutdown portaudio global handler at the end
    portaudio_global_handler = nullptr;
    return gui_retval;
}
