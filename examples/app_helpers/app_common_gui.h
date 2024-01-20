#pragma once

#include <stdint.h>
#include <functional>
#include <string_view>

struct CommonGui {
    std::function<void()> render_callback = nullptr;
    bool is_maximised = true;
    bool is_vsync = true;
    bool is_stop_rendering_on_defocus = true;
    size_t window_width = 1280;
    size_t window_height = 720;
    std::string_view window_title = "Radio App";
    std::string_view filepath_config = "imgui_radio.ini";
    std::string_view filepath_font_awesome_icon_ttf = "res/font_awesome.ttf";
    std::string_view filepath_regular_font_ttf = "res/Roboto-Regular.ttf";
    float regular_font_size = 15.0f;
    float font_awesome_icon_size = 16.0f;
    bool style_dark_theme = false;
    bool style_round_borders = true;
};

int render_common_gui_blocking(const CommonGui& gui);
