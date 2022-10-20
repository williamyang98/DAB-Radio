#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

// graphics code
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
#include <GLFW/glfw3.h> // Will drag system OpenGL headers

#include <io.h>
#include <fcntl.h>

#include "./getopt/getopt.h"

#include "fic/fic_decoder.h"
#include "fic/fig_processor.h"
#include "database/dab_database.h"
#include "database/dab_database_updater.h"
#include "msc/msc_decoder.h"
#include "audio/aac_frame_processor.h"
#include "radio_fig_handler.h"

#include <map>
#include <memory>
#include <thread>
#include <mutex>

#include "easylogging++.h"
#include "dab/logging.h"
INITIALIZE_EASYLOGGINGPP

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

class App;

void RenderApp(App* app);

class App: public FIC_Decoder::Callback
{
public:
    FIC_Decoder* fic_decoder;
    FIG_Processor* fig_processor;
    Radio_FIG_Handler* fig_handler;
    DAB_Database* dab_db;
    DAB_Database_Updater* dab_db_updater;
    std::map<subchannel_id_t, std::unique_ptr<MSC_Decoder>> msc_decoders;

    DAB_Database* complete_dab_db;
    bool is_subchannel_selected = false;
    subchannel_id_t selected_subchannel_id = 0;
    bool trigger_database_update = false;
    std::mutex mutex_database_update;
public:
    App() {
        fic_decoder = new FIC_Decoder();
        fig_processor = new FIG_Processor();
        fig_handler = new Radio_FIG_Handler();
        dab_db = new DAB_Database();
        dab_db_updater = new DAB_Database_Updater(dab_db);

        complete_dab_db = new DAB_Database();

        fig_handler->SetUpdater(dab_db_updater);
        fig_processor->SetHandler(fig_handler);
        fic_decoder->SetCallback(this);
    }
    ~App() {
        delete fic_decoder;
        delete fig_processor;
        delete fig_handler;
        delete dab_db;
        delete dab_db_updater;
        delete complete_dab_db;
    }
    void ProcessFrame(uint8_t* buf, const int N) {
        const int nb_frame_length = 28800;
        const int nb_symbols = 75;
        const int nb_sym_length = nb_frame_length / nb_symbols;

        const int nb_fic_symbols = 3;

        const auto* fic_buf = &buf[0];
        const auto* msc_buf = &buf[nb_fic_symbols*nb_sym_length];

        {
            const int nb_fic_length = nb_sym_length*nb_fic_symbols;
            const int nb_msc_length = nb_sym_length*(nb_symbols-nb_fic_symbols);

            // FIC: 3 symbols -> 12 FIBs -> 4 FIB groups
            // A FIB group contains FIGs (fast information group)
            const int nb_fic_groups = 4;
            const int nb_fic_group_length = nb_fic_length / nb_fic_groups;
            auto lock = std::unique_lock(mutex_database_update);
            for (int i = 0; i < nb_fic_groups; i++) {
                const auto* fic_group_buf = &fic_buf[i*nb_fic_group_length];
                fic_decoder->DecodeFIBGroup(fic_group_buf, i);
            }
        }

        // MSC: 72 symbols -> 4CIFs (18 symbols) 
        // 1 CIF = 18*1536*2 = 55296bits = 864*64bits = 864 CU (capacity units)
        {
            const int nb_msc_symbols = nb_symbols-nb_fic_symbols;

            const int nb_cifs = 4;
            const int nb_cif_symbols = nb_msc_symbols/nb_cifs;
            const int nb_cif_bytes = nb_sym_length*nb_cif_symbols;

            for (int i = 0; i < nb_cifs; i++) {
                const auto* cif_buf = &msc_buf[i*nb_cif_bytes];
                DecodeCIF(cif_buf, nb_cif_bytes, i);
            }
        }

        if (trigger_database_update) {
            trigger_database_update = false;
            auto lock = std::unique_lock(mutex_database_update);
            dab_db_updater->ExtractCompletedDatabase(*complete_dab_db);
        }

    }
    void DecodeCIF(const uint8_t* buf, const int N, const int cif_index) {
        if (!is_subchannel_selected) {
            return;
        }

        auto db = dab_db;

        auto res0 = msc_decoders.find(selected_subchannel_id);
        if (res0 == msc_decoders.end()) {
            auto& subchannels = db->lut_subchannels;
            auto res1 = subchannels.find(selected_subchannel_id);
            if (res1 == subchannels.end()) {
                LOG_MESSAGE("Selected invalid subchannel %d\n", selected_subchannel_id);
                return;
            }
            auto subchannel = res1->second;
            msc_decoders.insert({selected_subchannel_id, std::make_unique<MSC_Decoder>(*subchannel)});
        } 

        bool found_ascty = false;
        AudioServiceType ascty = AudioServiceType::DAB;

        auto res2 = db->lut_subchannel_to_service_component.find(selected_subchannel_id);
        if (res2 != db->lut_subchannel_to_service_component.end()) {
            if (res2->second->transport_mode == TransportMode::STREAM_MODE_AUDIO) {
                ascty = res2->second->audio_service_type;
                found_ascty = true;
            }
        }

        MSC_Decoder* msc_decoder = msc_decoders[selected_subchannel_id].get();
        const int nb_decoded_bytes = msc_decoder->DecodeCIF(buf, N);
        auto decoded_bytes_buf = msc_decoder->GetDecodedBytes();

        if (!found_ascty) {
            return;
        }

        if (ascty != AudioServiceType::DAB_PLUS) {
            return;
        }

        static auto frame_processor = AAC_Frame_Processor();
        if (nb_decoded_bytes > 0) {
            frame_processor.Process(decoded_bytes_buf, nb_decoded_bytes);
        }
    }
    void FilterPending() {
        auto& v = dab_db_updater->GetUpdaters();
        std::vector<UpdaterChild*> pending;
        std::vector<UpdaterChild*> complete;
        for (auto& e: v) {
            if (!e->IsComplete()) {
                pending.push_back(e);
            } else {
                complete.push_back(e);
            }
        }
    }
    virtual void OnDecodeFIBGroup(const uint8_t* buf, const int N, const int cif_index) {
        fig_processor->ProcessFIG(buf);
    }
};

void app_runner(
    App* app, uint8_t* buf, const int N, 
    bool* is_running, FILE* fp_in) {
    while (*is_running) {
        const auto nb_read = fread(buf, sizeof(uint8_t), N, fp_in);
        if (nb_read != N) {
            fprintf(stderr, "Failed to read %d bytes\n", N);
            break;
        }
        app->ProcessFrame(buf, N);
    }
}

void usage() {
    fprintf(stderr, 
        "process_frames, decoded DAB frame data\n\n"
        "\t[-i input filename (default: None)]\n"
        "\t    If no file is provided then stdin is used\n"
        "\t[-h (show usage)]\n"
    );
}

int main(int argc, char** argv) {
    char* rd_filename = NULL;

    int opt; 
    while ((opt = getopt(argc, argv, "i:h")) != -1) {
        switch (opt) {
        case 'i':
            rd_filename = optarg;
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

    _setmode(_fileno(fp_in), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    auto dab_loggers = RegisterLogging();

    el::Configurations defaultConf;
    defaultConf.setToDefault();
    defaultConf.set(el::Level::Error,   el::ConfigurationType::Enabled, "true");
    defaultConf.set(el::Level::Warning, el::ConfigurationType::Enabled, "true");
    defaultConf.set(el::Level::Info,    el::ConfigurationType::Enabled, "false");
    defaultConf.set(el::Level::Debug,   el::ConfigurationType::Enabled, "false");
    el::Loggers::reconfigureAllLoggers(defaultConf);

    // Number of bytes per OFDM frame in transmission mode I
    // NOTE: we are hard coding this because all other transmission modes have been deprecated
    const int N = 75*1536*2/8;
    auto buf = new uint8_t[N];
    auto app = App();
    bool is_running = true;

    auto runner_thread = std::thread(
        app_runner, 
        &app, buf, N, 
        &is_running, fp_in);

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
    ImGui::GetIO().IniFilename = "imgui_process_frames.ini";

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

        RenderApp(&app);

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

    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    is_running = false;
    runner_thread.join();

    return 0;
}

void RenderApp(App* app) {
    auto lock = std::unique_lock(app->mutex_database_update);
    auto db = app->dab_db;

    static char text_buffer[256];
    static char service_name[25] = {0};

    if (ImGui::Begin("Subchannels")) {
        if (ImGui::Button("Load database")) {
            app->trigger_database_update = true;
        }
        if (ImGui::BeginListBox("Subchannels list", ImVec2(ImGui::GetWindowWidth(), ImGui::GetWindowHeight()))) {
            for (auto& subchannel: db->subchannels) {

                auto res = db->lut_subchannel_to_service_component.find(subchannel.id);
                if (res != db->lut_subchannel_to_service_component.end()) {
                    auto res0 = db->lut_services.find(res->second->service_reference);
                    if (res0 != db->lut_services.end()) {
                        snprintf(service_name, 25, (res0->second)->label.c_str());
                    } else {
                        snprintf(service_name, 25, "");
                    }
                } else {
                    snprintf(service_name, 25, "");
                }

                snprintf(text_buffer, 256, "%d[%d+%d] uep=%u label=%s", 
                    subchannel.id,
                    subchannel.start_address,
                    subchannel.length,
                    subchannel.is_uep,
                    service_name);
                const bool is_selected = 
                    app->is_subchannel_selected && 
                    (subchannel.id == app->selected_subchannel_id);
                if (ImGui::Selectable(text_buffer, is_selected)) {
                    app->selected_subchannel_id = subchannel.id;
                    app->is_subchannel_selected = !is_selected;
                }

                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndListBox();
        }
    }
    ImGui::End();
}