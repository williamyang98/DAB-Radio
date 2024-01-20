#include "./app_common_gui.h"
#include <stdio.h>
#include <thread>
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>
#if IMGUI_IMPL_OPENGL_ES2
#include <GLES2/gl2.h>
#endif
#include <GLFW/glfw3.h>
#include "../gui/font_awesome_definitions.h"

static int is_main_window_focused = true;
static void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}
static void glfw_window_focus_callback(GLFWwindow* window, int focused) {
    is_main_window_focused = focused;
}

static const char* glfw_get_gsl_version();
static void imgui_setup_config_flags();
static void imgui_setup_fonts(const CommonGui& gui);
static void imgui_setup_styling(const CommonGui& gui);

int render_common_gui_blocking(const CommonGui& gui) {
    // create window
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        return 1;
    }
    const char* glsl_version = glfw_get_gsl_version();
    if (gui.is_maximised) {
        glfwWindowHint(GLFW_MAXIMIZED, 1);
    }
    GLFWwindow* const window = glfwCreateWindow(
        int(gui.window_width), int(gui.window_height), gui.window_title.data(),
        nullptr, nullptr
    );
    if (window == nullptr) {
        return 1;
    }
    glfwSetWindowFocusCallback(window, glfw_window_focus_callback);
    glfwMakeContextCurrent(window);
    if (gui.is_vsync) {
        glfwSwapInterval(1);
    }
    // setup imgui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    imgui_setup_config_flags();
    imgui_setup_fonts(gui);
    imgui_setup_styling(gui);
    ImGui::GetIO().IniFilename = gui.filepath_config.data();
    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);
    // Render loop
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (!is_main_window_focused && gui.is_stop_rendering_on_defocus) {
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            continue;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());
        if (gui.render_callback != nullptr) {
            gui.render_callback();
        }
        ImGui::Render();

        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(clear_color.x*clear_color.w, clear_color.y*clear_color.w, clear_color.z*clear_color.w, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        const ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            GLFWwindow* backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
        }
        glfwSwapBuffers(window);
    }
    // shutdown
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    ImPlot::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

const char* glfw_get_gsl_version() {
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
    return glsl_version;
}

void imgui_setup_config_flags() {
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;       // Enable Keyboard Controls
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;           // Enable Docking
    // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;         // Enable Multi-Viewport / Platform Windows
    //io.ConfigViewportsNoAutoMerge = true;
    //io.ConfigViewportsNoTaskBarIcon = true;
}

void imgui_setup_fonts(const CommonGui& gui) {
    auto& io = ImGui::GetIO();
    io.Fonts->AddFontFromFileTTF(gui.filepath_regular_font_ttf.data(), gui.regular_font_size);
    ImFontConfig icons_config;
    icons_config.MergeMode = true;
    icons_config.PixelSnapH = true;
    // NOTE: glphy_range requires 0,0 to be appended at the end since imgui uses
    //       this to check the end of buffer
    static const ImWchar glyph_range[4] = { ICON_MIN_FA, ICON_MAX_FA, 0, 0 };
    io.Fonts->AddFontFromFileTTF(
        gui.filepath_font_awesome_icon_ttf.data(), gui.font_awesome_icon_size, 
        &icons_config, glyph_range
    );
}

void imgui_setup_styling(const CommonGui& gui) {
    if (gui.style_dark_theme) {
        ImGui::StyleColorsDark();
    } else {
        ImGui::StyleColorsLight();
    }
    ImGuiStyle& style = ImGui::GetStyle();
    if (gui.style_round_borders) {
        // border size
        style.WindowBorderSize      = 1.0f;
        style.ChildBorderSize       = 1.0f;
        style.PopupBorderSize       = 1.0f;
        style.FrameBorderSize       = 1.0f;
        style.TabBorderSize         = 1.0f;
        // rounding properties
        style.WindowRounding        = 4.0f;
        style.ChildRounding         = 4.0f;
        style.FrameRounding         = 4.0f;
        style.PopupRounding         = 4.0f;
        style.ScrollbarRounding     = 12.0f;
        style.GrabRounding          = 4.0f;
        style.LogSliderDeadzone     = 4.0f;
        style.TabRounding           = 4.0f;
    }
    // theme color
    /*
    ImVec4* colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_WindowBg]               = ImVec4(0.08f, 0.08f, 0.08f, 0.94f);
    colors[ImGuiCol_ChildBg]                = ImVec4(0.09f, 0.09f, 0.09f, 0.00f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.07f, 0.07f, 0.07f, 0.00f);
    colors[ImGuiCol_FrameBg]                = ImVec4(0.28f, 0.28f, 0.28f, 0.54f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.78f, 0.60f, 0.25f, 0.64f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.80f, 0.46f, 0.17f, 0.64f);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.48f, 0.40f, 0.16f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.08f, 0.08f, 0.08f, 0.51f);
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.09f, 0.09f, 0.09f, 0.00f);
    colors[ImGuiCol_CheckMark]              = ImVec4(0.98f, 0.55f, 0.26f, 1.00f);
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.88f, 0.60f, 0.24f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.98f, 0.64f, 0.26f, 1.00f);
    colors[ImGuiCol_Button]                 = ImVec4(0.98f, 0.67f, 0.26f, 0.40f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.98f, 0.71f, 0.26f, 0.75f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.98f, 0.61f, 0.06f, 0.75f);
    colors[ImGuiCol_Header]                 = ImVec4(0.11f, 0.11f, 0.11f, 1.00f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.98f, 0.77f, 0.19f, 0.42f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.98f, 0.64f, 0.26f, 0.42f);
    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.75f, 0.51f, 0.10f, 0.78f);
    colors[ImGuiCol_SeparatorActive]        = ImVec4(0.75f, 0.49f, 0.10f, 1.00f);
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.98f, 0.67f, 0.26f, 0.20f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.98f, 0.71f, 0.26f, 0.67f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.98f, 0.69f, 0.26f, 0.95f);
    colors[ImGuiCol_Tab]                    = ImVec4(0.58f, 0.43f, 0.18f, 0.78f);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.98f, 0.69f, 0.26f, 0.78f);
    colors[ImGuiCol_TabActive]              = ImVec4(0.78f, 0.54f, 0.23f, 0.98f);
    colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.42f, 0.33f, 0.14f, 1.00f);
    colors[ImGuiCol_DockingPreview]         = ImVec4(0.98f, 0.69f, 0.26f, 0.70f);
    colors[ImGuiCol_TableRowBg]             = ImVec4(0.07f, 0.07f, 0.07f, 0.47f);
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.98f, 0.71f, 0.26f, 0.35f);
    colors[ImGuiCol_DragDropTarget]         = ImVec4(1.00f, 0.73f, 0.00f, 0.90f);
    colors[ImGuiCol_NavHighlight]           = ImVec4(0.98f, 0.76f, 0.26f, 1.00f);
    */
}
