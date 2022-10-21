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
#include "ofdm_symbol_mapper.h"

#include "dab_ofdm_params_ref.h"
#include "dab_prs_ref.h"
#include "dab_mapper_ref.h"

#include "gui/render_ofdm_demod.h"

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

class App
{
private:
    OFDM_Demodulator* demod;
    OFDM_Symbol_Mapper* mapper;
    OFDM_Demodulator::State demod_state;
    bool is_running = true;
public:
    bool is_wait_step = false;
    bool flag_step = false;
    bool flag_apply_rd_offset = false;
    bool flag_dump_frame = false;
    bool is_always_dump_frame = false;
public:
    App(OFDM_Demodulator* _demod, OFDM_Symbol_Mapper* _mapper)
    : demod(_demod), mapper(_mapper) {
        demod_state = OFDM_Demodulator::State::WAITING_NULL;
        demod->On_OFDM_Frame().Attach([this](const uint8_t* phases, const int nb_carriers, const int nb_symbols) {
            OnOFDMFrame(phases, nb_carriers, nb_symbols);
        });
    }
    void Run(FILE* fp_in, std::complex<uint8_t>* buf_rd, std::complex<float>* buf_rd_raw, const int block_size) {
        is_running = true;
        while (is_running) {

            while (!(flag_step) && (is_wait_step)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
            }
            flag_step = false;

            // NOTE: forcefully induce a single byte desync
            // This is required when the receiver is overloaded and something causes a single byte to be dropped
            // This causes the IQ values to become desynced and produce improper values
            // Inducing another single byte dropout will correct the stream
            if (flag_apply_rd_offset) {
                uint8_t dummy = 0x00;
                auto nb_read = fread(&dummy, sizeof(uint8_t), 1, fp_in);
                flag_apply_rd_offset = false;
            }

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
            demod_state = demod->GetState();
        }
    }
    void Stop() {
        is_running = false;
    }
    inline OFDM_Demodulator::State GetDemodulatorState() const { return demod_state; }

private:
    void OnOFDMFrame(const uint8_t* phases, const int nb_carriers, const int nb_symbols) {
        assert(mapper->GetTotalCarriers() == nb_carriers);
        assert(mapper->GetTotalSymbols() == nb_symbols);
        mapper->ProcessRawFrame(phases);
        if (flag_dump_frame || is_always_dump_frame) {
            const auto buf = mapper->GetOutputBuffer();
            const auto N = mapper->GetOutputBufferSize();
            OutputBuffer(buf, N);
            flag_dump_frame = false;
        }
    }
    void OutputBuffer(const uint8_t* buf, const int N) {
        fwrite(buf, sizeof(uint8_t), N, stdout);
        // for (int i = 0; i < N; i++) {
        //     for (int j = 0; j < 8; j++) {
        //         printf("%d, ", (buf[i] & (1u<<j)) != 0);
        //     }
        // }
        // for (int i = 0; i < nb_carriers*nb_symbols; i+=4) {
        //     uint8_t b = 0x00;
        //     b |= (phases[i+0]) << 0;
        //     b |= (phases[i+1]) << 2;
        //     b |= (phases[i+2]) << 4;
        //     b |= (phases[i+3]) << 6;
        //     fputc(b, stdout);
        // }
        // for (int i = 0; i < nb_carriers*nb_symbols; i++) {
        //     uint8_t b = phases[i];
        //     printf("%d, ", (b & 0b01) >> 0);
        //     printf("%d, ", (b & 0b10) >> 1);
        // }
    }
};

void usage() {
    fprintf(stderr, 
        "view_data, runs OFDM demodulation on raw IQ values with GUI\n\n"
        "\t[-b block size (default: 8192)]\n"
        "\t[-i input filename (default: None)]\n"
        "\t    If no file is provided then stdin is used\n"
        "\t[-M dab transmission mode (default: 1)]\n"
        "\t[-S toggle step mode (default: false)]\n"
        "\t[-D toggle frame output (default: false)]\n"
        "\t[-h (show usage)]\n"
    );
}

int main(int argc, char** argv) 
{
    int block_size = 8192;
    int transmission_mode = 1;
    bool is_step_mode = false;
    bool is_frame_output = false;
    char* rd_filename = NULL;

    int opt; 
    while ((opt = getopt(argc, argv, "b:i:M:SDh")) != -1) {
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
        case 'D':
            is_frame_output = true;
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
    _setmode(_fileno(stdout), _O_BINARY);
    
    const OFDM_Params ofdm_params = get_DAB_OFDM_params(transmission_mode);
    auto ofdm_prs_ref = new std::complex<float>[ofdm_params.nb_fft];
    get_DAB_PRS_reference(transmission_mode, ofdm_prs_ref, ofdm_params.nb_fft);
    auto ofdm_mapper_ref = new int[ofdm_params.nb_data_carriers];
    get_DAB_mapper_ref(ofdm_mapper_ref, ofdm_params.nb_data_carriers, ofdm_params.nb_fft);

    auto ofdm_demod = OFDM_Demodulator(ofdm_params, ofdm_prs_ref);
    // due to differential encoding, the PRS doesn't count 
    auto ofdm_mapper = OFDM_Symbol_Mapper(
        ofdm_mapper_ref, ofdm_params.nb_data_carriers, 
        ofdm_params.nb_frame_symbols-1);

    delete [] ofdm_prs_ref;
    delete [] ofdm_mapper_ref;

    auto app = App(&ofdm_demod, &ofdm_mapper);
    app.is_wait_step = is_step_mode;
    app.is_always_dump_frame = is_frame_output;
    auto app_runner = [&fp_in, &app, &buf_rd, &buf_rd_raw, block_size]() {
        app.Run(fp_in, buf_rd, buf_rd_raw, block_size);
    };
    auto proc_thread = new std::thread(app_runner);

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

        RenderSourceBuffer(buf_rd_raw, block_size);
        RenderOFDMDemodulator(&ofdm_demod, &ofdm_mapper);
        {
            ImGui::Begin("Input controls");
            if (ImGui::Button("Offset input stream")) {
                app.flag_apply_rd_offset = true;
            }
            ImGui::Checkbox("Enable stepping", &app.is_wait_step);
            if (app.is_wait_step) {
                if (ImGui::Button("Step")) {
                    app.flag_step = true;
                }
            }             

            ImGui::Checkbox("Enable continuous frame dump", &app.is_always_dump_frame);
            if (!app.is_always_dump_frame) {
                if (ImGui::Button("Dump next block")) {
                    app.flag_dump_frame = true;
                }
            } 

            ImGui::End();
        }

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

    app.Stop();
    proc_thread->join();

    return 0;
}
