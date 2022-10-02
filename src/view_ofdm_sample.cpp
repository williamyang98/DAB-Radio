#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <thread>

#include <complex>
#include <kiss_fft.h>

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

int main(int argc, char** argv) 
{
    constexpr int nfft = 2048;
    kiss_fft_cfg cfg = kiss_fft_alloc(nfft ,false , 0, 0);

    const int nb_null = 2656;
    const int nb_symbol = 2552;
    const int nb_fft = 2048;
    const int nb_prefix = nb_symbol - nb_fft;
    const int nb_frame_symbols = 76;
    const int nb_subcarriers = 1537;

    const float Fs = 2.048e6;
    const float Fbin = Fs/nfft;
    const float ofdm_freq_spacing = 1e3;

    const int total_rd = nb_frame_symbols*nb_symbol + nb_null;
    const int nb_ofdm_offset = -100;

    auto buf_rd = new std::complex<uint8_t>[total_rd];

    _setmode(_fileno(stdin), _O_BINARY);

    auto nb_read = fread((void*)buf_rd, sizeof(std::complex<uint8_t>), total_rd, stdin);
    if (nb_read != total_rd) {
        fprintf(stderr, "Failed to read in data\n");
        return 1;
    }

    auto buf_rd_raw = new std::complex<float>[total_rd];
    for (int i = 0; i < total_rd; i++) {
        auto& v = buf_rd[i];
        const float I = static_cast<float>(v.real() - 128);
        const float Q = static_cast<float>(v.imag() - 128);
        buf_rd_raw[i] = std::complex<float>(I, Q);
    }


    printf("Hello world!\n");

    auto cx_in = new std::complex<float>[nfft];
    auto cx_out = new std::complex<float>[nfft];

    auto fft_mag_avg = new float[nfft];
    for (int i = 0; i < nfft; i++) {
        fft_mag_avg[i] = 0;
    }

    auto fft_freq = new float[nfft];
    for (int i = 0; i < nfft; i++) {
        fft_freq[i] = (float)((i - nfft/2) * Fbin);
    }

    // Plot the average spectrum of the frame
    for (int i = 0; i < nb_frame_symbols; i++) {
        const int idx_sym_start = nb_null + i*nb_symbol + nb_ofdm_offset;
        const int idx_fft_start = idx_sym_start+nb_prefix; 

        kiss_fft(cfg, (kiss_fft_cpx*)(&buf_rd_raw[idx_fft_start]), (kiss_fft_cpx*)cx_out);
        for (int j = 0; j < nfft; j++) {
            fft_mag_avg[(j + nfft/2) % nfft] += 20*std::log10(std::abs(cx_out[j])) / (float)nb_frame_symbols;
        }
    }

    auto buf_pll = new std::complex<float>[total_rd];
    auto dt = new float[total_rd];

    for (int i = 0; i < total_rd; i++) {
        dt[i] = (float)i / (float)Fs;
    }

    // Get the frequency offset
    float freq_offset = 0;
    const float freq_corr_thresh = 0.001f;
    while (true) {
        constexpr float PI = 3.14159265f;
        // apply pll
        for (int i = 0; i < total_rd; i++) {
            auto pll = std::complex<float>(
                std::cosf(2*PI*freq_offset*dt[i]),
                std::sinf(2*PI*freq_offset*dt[i])
            );
            buf_pll[i] = buf_rd_raw[i] * pll; 
        }

        float freq_corr = 0;
        for (int i = 0; i < nb_frame_symbols; i++) {
            const int idx_sym_start = nb_null + i*nb_symbol + nb_ofdm_offset;

            auto* sym = &buf_pll[idx_sym_start];
            auto cyclic_prefix_correlation = std::complex<float>(0,0);
            for (int i = 0; i < nb_prefix; i++) {
                cyclic_prefix_correlation += std::conj(sym[i]) * sym[nb_fft+i];
            }
            freq_corr += std::atan2f(cyclic_prefix_correlation.imag(), cyclic_prefix_correlation.real());
        }
        freq_corr /= (float)nb_frame_symbols;

        if (std::abs(freq_corr) < freq_corr_thresh) {
            break;
        }

        freq_offset -= 0.5f * freq_corr/PI * ofdm_freq_spacing;
        freq_offset = std::fmodf(freq_offset + ofdm_freq_spacing/2.0f, ofdm_freq_spacing) - ofdm_freq_spacing/2.0f;

        printf("freq_corr: %.5f\n", freq_corr);
    }

    printf("Frequency offset: %.2f\n", freq_offset);

    // Plot a differential OFDM symbol
    int sym_idx = 0;

    auto ofdm_out = new float[nb_subcarriers];
    auto calculate_ofdm_symbol = [
        &buf_pll, &ofdm_out, &cfg, 
        nb_null, nb_symbol, nfft, nb_prefix, nb_subcarriers, nb_ofdm_offset]
        (const int sym_idx) 
    {
        const int idx_sym_start_0 = nb_null +  sym_idx   *nb_symbol + nb_ofdm_offset;
        const int idx_sym_start_1 = nb_null + (sym_idx+1)*nb_symbol + nb_ofdm_offset;

        auto* sym_0 = &buf_pll[idx_sym_start_0];
        auto* sym_1 = &buf_pll[idx_sym_start_1];

        auto* ofdm_0 = &sym_0[nb_prefix];
        auto* ofdm_1 = &sym_1[nb_prefix];

        auto Y0 = new std::complex<float>[nfft];
        auto Y1 = new std::complex<float>[nfft];
        auto Y_diff = new std::complex<float>[nfft];

        kiss_fft(cfg, (kiss_fft_cpx*)(ofdm_0), (kiss_fft_cpx*)Y0);
        kiss_fft(cfg, (kiss_fft_cpx*)(ofdm_1), (kiss_fft_cpx*)Y1);

        for (int i = 0; i < nfft; i++) {
            Y_diff[(i + nfft/2) % nfft] = std::conj(Y0[i]) * Y1[i];
        }

        for (int i = 0; i < nb_subcarriers; i++) {
            auto& v = Y_diff[i + nfft/2 - nb_subcarriers/2];
            ofdm_out[i] = std::atan2f(v.imag(), v.real());
        }

        delete Y0;
        delete Y1;
        delete Y_diff;
    };

    calculate_ofdm_symbol(sym_idx);


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
        "OFDM Sample Viewer", 
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

        ImGui::Begin("Controls");
        ImGui::Text("Testing");
        ImGui::End();

        ImGui::Begin("FFT");
        if (ImPlot::BeginPlot("FFT average")) {
            ImPlot::PlotLine("Magnitude", fft_freq, fft_mag_avg, nfft);
            ImPlot::EndPlot();
        }

        if (ImGui::SliderInt("Symbol", &sym_idx, 0, nb_frame_symbols-2)) {
            calculate_ofdm_symbol(sym_idx);
        }

        if (ImPlot::BeginPlot("OFDM symbol")) {
            ImPlot::PlotScatter("delta-phase", ofdm_out, nb_subcarriers);
            ImPlot::EndPlot();
        }
        ImGui::End();

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

    kiss_fft_free(cfg);
    delete cx_in;
    delete cx_out;
    delete buf_rd;


    return 0;
}