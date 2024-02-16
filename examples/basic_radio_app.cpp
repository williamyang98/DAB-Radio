#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#if _WIN32
#include <io.h>
#include <fcntl.h>
#endif

#include <argparse/argparse.hpp>
#include <fmt/core.h>
#include "basic_scraper/basic_scraper.h"
#include "./app_helpers/app_io_buffers.h"
#include "./app_helpers/app_ofdm_blocks.h"
#include "./app_helpers/app_radio_blocks.h"
#include "./app_helpers/app_viterbi_convert_block.h"
#include "./app_helpers/app_logging.h"

#if !BUILD_COMMAND_LINE
#include "./app_helpers/app_audio.h"
#include "./audio/audio_pipeline.h"
#include "./audio/portaudio_sink.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include "./app_helpers/app_common_gui.h"
#include "./gui/ofdm/render_ofdm_demod.h"
#include "./gui/ofdm/render_profiler.h"
#include "./gui/basic_radio/basic_radio_view_controller.h"
#include "./gui/basic_radio/render_basic_radio.h"
#include "./gui/audio/render_portaudio_controls.h"
#endif

void init_parser(argparse::ArgumentParser& parser) {
    parser.add_argument("-i", "--input")
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
    parser.add_argument("--configuration")
        .default_value(std::string("dab+ofdm"))
        .choices("dab+ofdm", "ofdm", "dab")
        .metavar("CONFIG")
        .nargs(1).required()
        .help("Use OFDM demodulator or/and DAB radio (dab+ofdm, ofdm, dab)");
    // ofdm settings
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
    parser.add_argument("--ofdm-enable-output")
        .default_value(false).implicit_value(true)
        .help("OFDM demodulator output is written to a file");
    parser.add_argument("--ofdm-output")
        .default_value(std::string(""))
        .metavar("OUTPUT_FILEPATH")
        .nargs(1).required()
        .help("Filename of output of OFDM demodulator (defaults to stdout)");
    parser.add_argument("--ofdm-output-hard-bytes")
        .default_value(false).implicit_value(true)
        .help("Output of OFDM demodulator is converted from soft bits to hard bytes (8x compression)");
    // radio settings
    parser.add_argument("--radio-total-threads")
        .default_value(size_t(1)).scan<'u', size_t>()
        .metavar("TOTAL_THREADS")
        .nargs(1).required()
        .help("Number of basic radio threads (0 = max number of threads)");
    parser.add_argument("--radio-enable-logging")
        .default_value(false).implicit_value(true)
        .help("Enable verbose logging for radio");
    parser.add_argument("--radio-input-hard-bytes")
        .default_value(false).implicit_value(true)
        .help("Input of radio is converted from hard bytes to soft bits (unpack compression)");
    // scraper settings
    parser.add_argument("--scraper-enable")
        .default_value(false).implicit_value(true)
        .help("Radio scraper will be used to save radio data to a directory");
    parser.add_argument("--scraper-output")
        .default_value(std::string("data/scraper"))
        .metavar("OUTPUT_FOLDER")
        .nargs(1).required()
        .help("Output folder for scraper");
    parser.add_argument("--scraper-disable-logging")
        .default_value(false).implicit_value(true)
        .help("Disable verbose logging for scraper");
    parser.add_argument("--scraper-disable-auto")
        .default_value(false).implicit_value(true)
        .help("Disable automatic scraping of new channels");
    // other
#if !BUILD_COMMAND_LINE
    parser.add_argument("--audio-no-auto-select")
        .default_value(false).implicit_value(true)
        .help("Disable automatic selection of output audio device");
#else
    parser.add_argument("--radio-enable-benchmark")
        .default_value(false).implicit_value(true)
        .help("Enables data and audio decoding for cli benchmarking");
#endif
}

struct Args {
    std::string input_file; 
    int transmission_mode;
    bool is_ofdm_used;
    bool is_dab_used;
    // ofdm settings
    size_t ofdm_block_size;
    size_t ofdm_total_threads;
    bool ofdm_disable_coarse_freq;
    bool ofdm_enable_output;
    std::string ofdm_output;
    bool ofdm_output_hard_bytes;
    // radio settings
    size_t radio_total_threads;
    bool radio_enable_logging;
    bool radio_input_hard_bytes;
    // scraper settings
    bool scraper_enable;
    std::string scraper_output;
    bool scraper_disable_logging;
    bool scraper_disable_auto;
    // other
#if !BUILD_COMMAND_LINE
    bool audio_no_auto_select;
#else
    bool radio_enable_benchmark;
#endif
};

Args get_args_from_parser(const argparse::ArgumentParser& parser) {
    Args args;
    args.input_file = parser.get<std::string>("--input");
    args.transmission_mode = parser.get<int>("--transmission-mode");
    auto configuration = parser.get<std::string>("--configuration");
    args.is_ofdm_used = true;
    args.is_dab_used = true;
    if (configuration.compare("ofdm") == 0) {
        args.is_dab_used = false;
    } else if (configuration.compare("dab") == 0) {
        args.is_ofdm_used = false;
    }
    // ofdm settings
    args.ofdm_block_size = parser.get<size_t>("--ofdm-block-size");
    args.ofdm_total_threads = parser.get<size_t>("--ofdm-total-threads");
    args.ofdm_disable_coarse_freq = parser.get<bool>("--ofdm-disable-coarse-freq");
    args.ofdm_enable_output = parser.get<bool>("--ofdm-enable-output");
    args.ofdm_output = parser.get<std::string>("--ofdm-output");
    args.ofdm_output_hard_bytes = parser.get<bool>("--ofdm-output-hard-bytes");
    // radio settings
    args.radio_total_threads = parser.get<size_t>("--radio-total-threads");
    args.radio_enable_logging = parser.get<bool>("--radio-enable-logging");
    args.radio_input_hard_bytes = parser.get<bool>("--radio-input-hard-bytes");
    // scraper settings
    args.scraper_enable = parser.get<bool>("--scraper-enable");
    args.scraper_output = parser.get<std::string>("--scraper-output");
    args.scraper_disable_logging = parser.get<bool>("--scraper-disable-logging");
    args.scraper_disable_auto = parser.get<bool>("--scraper-disable-auto");
    // other
#if !BUILD_COMMAND_LINE
    args.audio_no_auto_select = parser.get<bool>("--audio-no-auto-select");
#else
    args.radio_enable_benchmark = parser.get<bool>("--radio-enable-benchmark");
#endif
    return args;
}


INITIALIZE_EASYLOGGINGPP
int main(int argc, char** argv) {
#if !BUILD_COMMAND_LINE
    const char* PROGRAM_NAME = "basic_radio_app";
    const char* PROGRAM_DESCRIPTION = "Radio app that reads from a file with a gui";
#else
    const char* PROGRAM_NAME = "basic_radio_app_cli";
    const char* PROGRAM_DESCRIPTION = "Radio app that reads from a file";
#endif
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
    if (args.ofdm_block_size == 0) {
        fprintf(stderr, "OFDM block size cannot be zero\n");
        return 1;
    }

    FILE* fp_in = stdin;
    if (!args.input_file.empty()) { 
        fp_in = fopen(args.input_file.c_str(), "rb");
        if (fp_in == nullptr) {
            fprintf(stderr, "Failed to open input file: '%s'\n", args.input_file.c_str());
            return 1;
        }
    }

    FILE* fp_ofdm_out = stdout;
    if (args.is_ofdm_used && args.ofdm_enable_output && !args.ofdm_output.empty()) {
        fp_ofdm_out = fopen(args.ofdm_output.c_str(), "wb+");
        if (fp_ofdm_out == nullptr) {
            fprintf(stderr, "Failed to open output file: '%s'\n", args.ofdm_output.c_str());
            return 1;
        }
    }

#if _WIN32
    _setmode(_fileno(fp_in), _O_BINARY);
    _setmode(_fileno(fp_ofdm_out), _O_BINARY);
#endif
    setup_easylogging(false, args.radio_enable_logging, !args.scraper_disable_logging); 

    const auto dab_params = get_dab_parameters(args.transmission_mode);
    // setup ofdm 
    std::shared_ptr<OFDM_Block> ofdm_block = nullptr;
    auto ofdm_output_splitter = std::shared_ptr<OutputSplitter<viterbi_bit_t>>();
    if (args.is_ofdm_used) {
        ofdm_block = std::make_shared<OFDM_Block>(args.transmission_mode, args.ofdm_total_threads);
        ofdm_output_splitter = std::make_shared<OutputSplitter<viterbi_bit_t>>();
        ofdm_block->set_output_stream(ofdm_output_splitter);
        auto& config = ofdm_block->get_ofdm_demod().GetConfig();
        config.sync.is_coarse_freq_correction = !args.ofdm_disable_coarse_freq;
    }
    // setup radio
    std::shared_ptr<Basic_Radio_Block> radio_block = nullptr;
    if (args.is_dab_used) {
        radio_block = std::make_shared<Basic_Radio_Block>(args.transmission_mode, args.radio_total_threads);
    }
    // setup input
    std::shared_ptr<FileWrapper> file_in = nullptr;
    if (args.is_ofdm_used) {
        auto raw_iq_in = std::make_shared<InputFile<RawIQ>>(fp_in);
        auto ofdm_convert_raw_iq = std::make_shared<OFDM_Convert_RawIQ>();
        ofdm_convert_raw_iq->set_input_stream(raw_iq_in);
        ofdm_block->set_input_stream(ofdm_convert_raw_iq);
        file_in = raw_iq_in;
    } else {
        if (args.radio_input_hard_bytes) {
            auto hard_bytes_in = std::make_shared<InputFile<uint8_t>>(fp_in);
            auto convert_viterbi_hard_to_soft = std::make_shared<Convert_Viterbi_Bytes_to_Bits>();
            convert_viterbi_hard_to_soft->set_input_stream(hard_bytes_in);
            radio_block->set_input_stream(convert_viterbi_hard_to_soft);
            file_in = hard_bytes_in;
        } else {
            auto soft_bits_in = std::make_shared<InputFile<viterbi_bit_t>>(fp_in);
            radio_block->set_input_stream(soft_bits_in);
            file_in = soft_bits_in;
        }
    }
    // setup output
    std::shared_ptr<FileWrapper> file_out = nullptr;
    if (args.is_ofdm_used && args.ofdm_enable_output) {
        if (args.ofdm_output_hard_bytes) {
            auto convert_viterbi_soft_to_hard = std::make_shared<Convert_Viterbi_Bytes_to_Bits>();
            auto hard_bytes_out = std::make_shared<OutputFile<uint8_t>>(fp_ofdm_out);
            ofdm_output_splitter->add_output_stream(convert_viterbi_soft_to_hard);
            convert_viterbi_soft_to_hard->set_output_stream(hard_bytes_out);
            file_out = hard_bytes_out;
        } else {
            auto soft_bits_out = std::make_shared<OutputFile<viterbi_bit_t>>(fp_ofdm_out);
            ofdm_output_splitter->add_output_stream(soft_bits_out);
            file_out = soft_bits_out;
        }
    }
    // setup connection between ofdm to dab
    std::shared_ptr<ThreadedRingBuffer<viterbi_bit_t>> ofdm_to_radio_buffer = nullptr;
    if (args.is_ofdm_used && args.is_dab_used) {
        ofdm_to_radio_buffer = std::make_shared<ThreadedRingBuffer<viterbi_bit_t>>(dab_params.nb_frame_bits*2);
        ofdm_output_splitter->add_output_stream(ofdm_to_radio_buffer);
        radio_block->set_input_stream(ofdm_to_radio_buffer);
    }
    // scraper
    if (args.is_dab_used && args.scraper_enable) {
        auto basic_scraper = std::make_shared<BasicScraper>(args.scraper_output);
        fprintf(stderr, "basic scraper is writing to folder '%s'\n", args.scraper_output.c_str()); 
        BasicScraper::attach_to_radio(basic_scraper, radio_block->get_basic_radio());
        radio_block->get_basic_radio().On_Audio_Channel().Attach(
            [](subchannel_id_t subchannel_id, Basic_Audio_Channel& channel) {
                auto& controls = channel.GetControls();
                controls.SetIsDecodeAudio(true);
                controls.SetIsDecodeData(true);
                controls.SetIsPlayAudio(false);
            }
        );
    }
#if BUILD_COMMAND_LINE
    // benchmark
    if (args.is_dab_used && args.radio_enable_benchmark) {
        radio_block->get_basic_radio().On_Audio_Channel().Attach(
            [](subchannel_id_t subchannel_id, Basic_Audio_Channel& channel) {
                auto& controls = channel.GetControls();
                controls.SetIsDecodeAudio(true);
                controls.SetIsDecodeData(true);
                controls.SetIsPlayAudio(true); // also stress test audio ring buffer ingest
                fprintf(stderr, "benchmarking DAB+ subchannel %u\n", subchannel_id);
            }
        );
    }
#else
    // audio
    std::unique_ptr<PortAudioGlobalHandler> portaudio_global_handler = nullptr;
    std::shared_ptr<AudioPipeline> audio_pipeline = nullptr;
    std::shared_ptr<PortAudioThreadedActions> portaudio_threaded_actions = nullptr;
    if (args.is_dab_used) {
        portaudio_global_handler = std::make_unique<PortAudioGlobalHandler>();
        audio_pipeline = std::make_shared<AudioPipeline>();
        attach_audio_pipeline_to_radio(audio_pipeline, radio_block->get_basic_radio());
        portaudio_threaded_actions = std::make_shared<PortAudioThreadedActions>();
        portaudio_threaded_actions->refresh();
    }
    // gui
    std::shared_ptr<BasicRadioViewController> radio_view_controller = nullptr;
    if (args.is_dab_used) {
        radio_view_controller = std::make_shared<BasicRadioViewController>();
    }
    const auto window_title = fmt::format(
        "Basic Radio App ({}{}{})",
        args.is_ofdm_used ? "OFDM" : "",
        (args.is_ofdm_used && args.is_dab_used) ? "+" : "",
        args.is_dab_used ? "DAB" : ""
    );
    CommonGui gui;
    gui.window_title = window_title;
    gui.render_callback = [ofdm_block, radio_block, portaudio_threaded_actions, audio_pipeline, radio_view_controller, args]() {
        if (args.is_ofdm_used) {
            if (ImGui::Begin("OFDM Demodulator")) {
                ImGuiID dockspace_id = ImGui::GetID("Demodulator Dockspace");
                ImGui::DockSpace(dockspace_id);
                RenderSourceBuffer(ofdm_block->get_buffer());
                RenderOFDMDemodulator(ofdm_block->get_ofdm_demod());
                RenderProfiler();
            }
            ImGui::End();
        }

        if (args.is_dab_used) {
            if (ImGui::Begin("Simple View###simple_view")) {
                ImGuiID dockspace_id = ImGui::GetID("Simple View Dockspace");
                ImGui::DockSpace(dockspace_id);
                if (ImGui::Begin("Audio Controls")) {
                    RenderPortAudioControls(*(portaudio_threaded_actions.get()), audio_pipeline);
                    RenderVolumeSlider(audio_pipeline->get_global_gain());
                }
                ImGui::End();
                RenderBasicRadio(radio_block->get_basic_radio(), *(radio_view_controller.get()));
            }
            ImGui::End();
        }
    };
#endif
    // threads
    std::unique_ptr<std::thread> thread_ofdm = nullptr;
    if (args.is_ofdm_used) {
        const size_t block_size = args.ofdm_block_size;
        thread_ofdm = std::make_unique<std::thread>([ofdm_block, block_size, ofdm_to_radio_buffer]() {
            ofdm_block->run(block_size);
            fprintf(stderr, "ofdm thread finished\n");
            if (ofdm_to_radio_buffer != nullptr) ofdm_to_radio_buffer->close();
        });
    }
    std::unique_ptr<std::thread> thread_radio = nullptr;
    if (args.is_dab_used) {
        thread_radio = std::make_unique<std::thread>([radio_block]() {
            radio_block->run();
            fprintf(stderr, "radio thread finished\n");
        });
    }
#if !BUILD_COMMAND_LINE
    std::unique_ptr<std::thread> thread_select_default_audio = nullptr;
    if (args.is_dab_used && !args.audio_no_auto_select) {
        thread_select_default_audio = std::make_unique<std::thread>([portaudio_threaded_actions, audio_pipeline]() {
            const PaDeviceIndex device_index = get_default_portaudio_device_index();
            portaudio_threaded_actions->select_device(device_index, audio_pipeline); 
        });
    }
#endif
    // shutdown
#if !BUILD_COMMAND_LINE
    const int gui_retval = render_common_gui_blocking(gui);
    if (thread_select_default_audio != nullptr) thread_select_default_audio->join();
    if (file_in != nullptr) file_in->close();
    if (file_out != nullptr) file_out->close();
    if (thread_ofdm != nullptr) thread_ofdm->join();
    if (ofdm_to_radio_buffer != nullptr) ofdm_to_radio_buffer->close();
    if (thread_radio != nullptr) thread_radio->join();
    ofdm_block = nullptr;
    radio_block = nullptr;
    portaudio_threaded_actions = nullptr;
    audio_pipeline = nullptr;
    // NOTE: need to shutdown portaudio global handler at the end
    portaudio_global_handler = nullptr;
    return gui_retval;
#else
    if (thread_ofdm != nullptr) thread_ofdm->join();
    if (ofdm_to_radio_buffer != nullptr) ofdm_to_radio_buffer->close();
    if (thread_radio != nullptr) thread_radio->join();
    if (file_in != nullptr) file_in->close();
    if (file_out != nullptr) file_out->close();
    ofdm_block = nullptr;
    radio_block = nullptr;
    return 0;
#endif
}

