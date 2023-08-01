#include "imgui_skeleton.h"

// graphics code
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
#include <GLFW/glfw3.h> // Will drag system OpenGL headers

// standard library
#include <stdio.h>
#include <thread>

// [Win32] Our example includes a copy of glfw3.lib pre-compiled with VS2010 to maximize ease of testing and compatibility with old VS compilers.
// To link with VS2010-era libraries, VS2015+ requires linking with legacy_stdio_definitions.lib, which we do using this pragma.
// Your own project should not be affected, as you are likely to link with a newer binary of GLFW that is adequate for your version of Visual Studio.
#if defined(_MSC_VER) && (_MSC_VER >= 1900) && !defined(IMGUI_DISABLE_WIN32_FUNCTIONS)
#pragma comment(lib, "legacy_stdio_definitions")
#endif

int is_main_window_focused = true;
static void glfw_error_callback(int error, const char* description) {
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

// this occurs when we minimise or change focus to another window
static void glfw_window_focus_callback(GLFWwindow* window, int focused) {
    is_main_window_focused = focused;
}

// Placeholder skeleton for imgui
void ImguiSkeleton::BeforeGLFWInit() {
    glfwWindowHint(GLFW_MAXIMIZED, 1);
}

GLFWwindow* ImguiSkeleton::Create_GLFW_Window() {
    GLFWwindow* window = glfwCreateWindow(
        1280, 720, 
        "Placeholder title", 
        NULL, NULL);
    return window;
}

void ImguiSkeleton::AfterGLFWInit() {
    glfwSwapInterval(1); // Enable vsync
}

void ImguiSkeleton::AfterImguiContextInit() {
    ImGuiIO& io = ImGui::GetIO();
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
}

void ImguiSkeleton::AfterShutdown() {
    // If you are using implot, we can place the DestroyContext call here
}

// Inject skeleton into our imgui app
int RenderImguiSkeleton(ImguiSkeleton* runner) {
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
    runner->BeforeGLFWInit();
    GLFWwindow* window = runner->Create_GLFW_Window();
    if (window == NULL) {
        return 1;
    }
    glfwSetWindowFocusCallback(window, glfw_window_focus_callback);
    glfwMakeContextCurrent(window);
    runner->AfterGLFWInit();

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    runner->AfterImguiContextInit();

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

        runner->Render();

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
        const ImGuiIO& io = ImGui::GetIO();
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
    runner->AfterShutdown();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

void ImGuiSetupCustomConfig(void) {
    ImGuiStyle& style = ImGui::GetStyle();

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
