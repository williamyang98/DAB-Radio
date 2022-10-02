#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <thread>

// graphics code
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
#include <GLFW/glfw3.h> // Will drag system OpenGL headers

// implot library
#include "implot.h"

#include <io.h>
#include <fcntl.h>

#include "./getopt/getopt.h"

#include <complex>
#include "ofdm_demodulator.h"
#include "dab_ofdm_params_ref.h"
#include "dab_prs_ref.h"

#define PRINT_LOG 1
#if PRINT_LOG 
  #define LOG_MESSAGE(...) fprintf(stderr, ##__VA_ARGS__)
#else
  #define LOG_MESSAGE(...) (void)0
#endif


// [Win32] Our example includes a copy of glfw3.lib pre-compiled with VS2010 to maximize ease of testing and compatibility with old VS compilers.
// To link with VS2010-era libraries, VS2015+ requires linking with legacy_stdio_definitions.lib, which we do using this pragma.
// Your own project should not be affected, as you are likely to link with a newer binary of GLFW that is adequate for your version of Visual Studio.
#if defined(_MSC_VER) && (_MSC_VER >= 1900) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#pragma comment(lib, "legacy_stdio_definitions")
#endif

int is_main_window_focused = true;

static void glfw_error_callback(int error, const char* description)
{
    LOG_MESSAGE("Glfw Error %d: %s\n", error, description);
}

// this occurs when we minimise or change focus to another window
static void glfw_window_focus_callback(GLFWwindow* window, int focused)
{
    is_main_window_focused = focused;
}

static auto state = OFDM_Demodulator::State::WAITING_NULL;

void app_thread(
    FILE* fp_in, OFDM_Demodulator* demod, 
    std::complex<uint8_t>* buf_rd, std::complex<float>* buf_rd_raw,
    bool* is_running,
    const int block_size) {

    while (*is_running) {
        auto nb_read = fread((void*)buf_rd, sizeof(std::complex<uint8_t>), block_size, fp_in);
        if (nb_read != block_size) {
            fprintf(stderr, "Failed to read in data\n");
            break;
        }

        for (int i = 0; i < block_size; i++) {
            auto& v = buf_rd[i];
            const float I = static_cast<float>(v.real()) - 127.5f;
            const float Q = static_cast<float>(v.imag()) - 127.5f;
            buf_rd_raw[i] = std::complex<float>(I, Q);
        }

        demod->ProcessBlock(buf_rd_raw, block_size);
        state = demod->GetState();
    }
}

void usage() {
    fprintf(stderr, 
        "view_data, runs OFDM demodulation on raw IQ values with GUI\n\n"
        "\t[-b block size (default: 8192)]\n"
        "\t[-i input filename (default: None)]\n"
        "\t    If no file is provided then stdin is used\n"
        "\t[-M dab transmission mode (default: 1)]\n"
        "\t[-S toggle step mode (default: false)]\n"
        "\t[-h (show usage)]\n"
    );
}

int main(int argc, char** argv) 
{
    int block_size = 8192;
    int transmission_mode = 1;
    bool is_step_mode = false;
    char* rd_filename = NULL;

    int opt; 
    while ((opt = getopt(argc, argv, "b:i:M:Sh")) != -1) {
        switch (opt) {
        case 'b':
            block_size = (int)(atof(optarg));
            if (block_size <= 0) {
                fprintf(stderr, "Block size must be positive (%d)\n", block_size); 
                return 1;
            }
            break;
        case 'i':
            rd_filename = optarg;
            break;
        case 'M':
            transmission_mode = (int)(atof(optarg));
            if (transmission_mode <= 0 || transmission_mode > 4) {
                fprintf(stderr, "Transmission modes: I,II,III,IV are supported not (%d)\n", transmission_mode);
                return 1;
            }
            break;
        case 'S':
            is_step_mode = true;
            break;
        case 'h':
        default:
            usage();
            return 0;
        }
    }

    // app startup
    FILE* fp_in = stdin;
    if (rd_filename != NULL) {
        errno_t err = fopen_s(&fp_in, rd_filename, "r");
        if (err != 0) {
            LOG_MESSAGE("Failed to open file for reading\n");
            return 1;
        }
    }

    auto buf_rd = new std::complex<uint8_t>[block_size];
    auto buf_rd_raw = new std::complex<float>[block_size];

    _setmode(_fileno(fp_in), _O_BINARY);
    
    const OFDM_Params ofdm_params = get_DAB_OFDM_params(transmission_mode);
    auto ofdm_prs_ref = new std::complex<float>[ofdm_params.nb_fft];
    get_DAB_PRS_reference(transmission_mode, ofdm_prs_ref, ofdm_params.nb_fft);

    auto ofdm_demod = OFDM_Demodulator(ofdm_params, ofdm_prs_ref);
    delete ofdm_prs_ref;

    bool is_running = !is_step_mode;
    auto proc_thread = new std::thread(
        app_thread, 
        fp_in, &ofdm_demod, 
        buf_rd, buf_rd_raw, 
        &is_running, block_size);

    // Setup window
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        return 1;
    }

        // Decide GL+GLSL versions
    #if defined(IMGUI_IMPL_OPENGL_ES2)
        // GL ES 2.0 + GLSL 100
        const char* glsl_version = "#version 100";
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    #elif defined(__APPLE__)
        // GL 3.2 + GLSL 150
        const char* glsl_version = "#version 150";
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // Required on Mac
    #else
        // GL 3.0 + GLSL 130
        const char* glsl_version = "#version 130";
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
        //glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);  // 3.2+ only
        //glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);            // 3.0+ only
    #endif

    // Create window with graphics context
    glfwWindowHint(GLFW_MAXIMIZED, 1);
    GLFWwindow* window = glfwCreateWindow(
        1280, 720, 
        "OFDM Demodulator Telemetry", 
        NULL, NULL);

    if (window == NULL) {
        return 1;
    }

    glfwSetWindowFocusCallback(window, glfw_window_focus_callback);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
    // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows
    //io.ConfigViewportsNoAutoMerge = true;
    //io.ConfigViewportsNoTaskBarIcon = true;

    // Setup Dear ImGui style
    // ImGui::StyleColorsDark();
    ImGui::StyleColorsLight();

    // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Main loop
    while (!glfwWindowShouldClose(window))
    {
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        glfwPollEvents();

        if (!is_main_window_focused) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            continue;
        }

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();

        ImGui::NewFrame();

        ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());

        {
            ImGui::Begin("Sampling buffer");
            if (ImGui::Button("Read block")) {
                auto nb_read = fread((void*)buf_rd, sizeof(std::complex<uint8_t>), block_size, fp_in);
                if (nb_read != block_size) {
                    fprintf(stderr, "Failed to read in data\n");
                    return 1;
                }

                for (int i = 0; i < block_size; i++) {
                    auto& v = buf_rd[i];
                    const float I = static_cast<float>(v.real()) - 127.5f;
                    const float Q = static_cast<float>(v.imag()) - 127.5f;
                    buf_rd_raw[i] = std::complex<float>(I, Q);
                }

                ofdm_demod.ProcessBlock(buf_rd_raw, block_size);
                state = ofdm_demod.GetState();
            }

            if (ImPlot::BeginPlot("Block")) {
                auto buf = reinterpret_cast<float*>(buf_rd_raw);
                ImPlot::SetupAxisLimits(ImAxis_Y1, -128, 128, ImPlotCond_Once);
                ImPlot::PlotLine("Real", &buf[0], block_size, 1.0f, 0, 0, 0, 2*sizeof(float));
                ImPlot::SetupAxisLimits(ImAxis_Y1, -128, 128, ImPlotCond_Once);
                ImPlot::PlotLine("Imag", &buf[1], block_size, 1.0f, 0, 0, 0, 2*sizeof(float));
                ImPlot::EndPlot();
            }
            ImGui::End();
        }

        {
            ImGui::Begin("DQPSK data");
            const int total_symbols = ofdm_demod.params.nb_frame_symbols;
            static int symbol_index = 0;

            ImGui::SliderInt("DQPSK Symbol Index", &symbol_index, 0, total_symbols-2);

            static double dqsk_decision_boundaries[3] = {-3.1415/2, 0, 3.1415/2};

            if (ImPlot::BeginPlot("DQPSK data")) {
                const int total_carriers = ofdm_demod.params.nb_data_carriers;
                const int buffer_offset = symbol_index*total_carriers;
                auto buf = &ofdm_demod.ofdm_frame_data[buffer_offset];
                ImPlot::SetupAxisLimits(ImAxis_Y1, -4, +4, ImPlotCond_Once);
                ImPlot::PlotScatter("Delta-Phase", buf, total_carriers);
                for (int i = 0; i < 3; i++) {
                    ImPlot::DragLineY(i, &dqsk_decision_boundaries[i], ImVec4(1,0,0,1), 1.0f);
                }
                ImPlot::EndPlot();
            }
            ImGui::End();
        }

        {
            ImGui::Begin("Impulse response");
            if (ImPlot::BeginPlot("Impulse response")) {
                auto buf = ofdm_demod.prs_impulse_response;
                const int N = ofdm_demod.params.nb_fft;
                ImPlot::SetupAxisLimits(ImAxis_Y1, 60, 150, ImPlotCond_Once);
                ImPlot::PlotLine("Impulse response", buf, N);
                ImPlot::EndPlot();
            }
            ImGui::End();
        }

        {
            ImGui::Begin("Null symbol spectrum");
            if (ImPlot::BeginPlot("Null symbol")) {
                auto buf = ofdm_demod.null_sym_data;
                const int N = ofdm_demod.params.nb_fft;
                ImPlot::SetupAxisLimits(ImAxis_Y1, 20, 90, ImPlotCond_Once);
                ImPlot::PlotLine("Null symbol", buf, N);
                ImPlot::EndPlot();
            }
            ImGui::End();
        }

        {
            ImGui::Begin("Data symbol spectrum");
            if (ImPlot::BeginPlot("Data symbol spectrum")) {
                auto buf = ofdm_demod.ofdm_magnitude_avg;
                const int N = ofdm_demod.params.nb_fft;
                ImPlot::SetupAxisLimits(ImAxis_Y1, 20, 90, ImPlotCond_Once);
                ImPlot::PlotLine("Data symbol", buf, N);
                ImPlot::EndPlot();
            }
            ImGui::End();
        }

        {
            ImGui::Begin("Controls/Stats");

            ImGui::Checkbox("Force fine freq", &ofdm_demod.is_update_fine_freq);
            switch (state) {
            case OFDM_Demodulator::State::WAITING_NULL:
                ImGui::Text("State: Waiting null");
                break;
            case OFDM_Demodulator::State::READING_OFDM_FRAME:
                ImGui::Text("State: Reading data symbol");
                break;
            case OFDM_Demodulator::State::READING_NULL_SYMBOL:
                ImGui::Text("State: Reading null symbol");
                break;
            default:
                ImGui::Text("State: Unknown");
                break;
            }
            ImGui::Text("Fine freq: %.3f", ofdm_demod.freq_fine_offset);
            ImGui::Text("Signal level: %.2f", ofdm_demod.signal_l1_average);
            ImGui::Text("Symbols read: %d/%d", 
                ofdm_demod.curr_ofdm_symbol,
                ofdm_demod.params.nb_frame_symbols);
            ImGui::Text("Frames read: %d", ofdm_demod.total_frames_read);
            ImGui::Text("Frames desynced: %d", ofdm_demod.total_frames_desync);

            ImGui::End();
        }

        ImGui::End();

        // Rendering
        ImGui::Render();

        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    	
        // Update and Render additional Platform Windows
        // (Platform functions may change the current OpenGL context, so we save/restore it to make it easier to paste this code elsewhere.
        //  For this specific demo app we could also call glfwMakeContextCurrent(window) directly)
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            GLFWwindow* backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
        }

        glfwSwapBuffers(window);
    }


    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();

    ImPlot::DestroyContext();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    is_running = false;
    proc_thread->join();

    return 0;
}