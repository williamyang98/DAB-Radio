#pragma once

#include <stdint.h>
#include <unordered_map>
#include <memory>

#include "imgui.h"
#include "dab/database/dab_database_entities.h"
#include "basic_radio/basic_slideshow.h"
#include "texture.h"

using slideshow_key_t = std::pair<subchannel_id_t, mot_transport_id_t>;

class SimpleSlideshowController 
{
public:
private:
    std::unordered_map<uint32_t, std::unique_ptr<Texture>> slideshows;
public:
    Texture* AddSlideshow(slideshow_key_t id, const uint8_t* data, const int N);
    Texture* GetSlideshow(slideshow_key_t id);
private:
    uint32_t GetKey(slideshow_key_t key);
};

class SimpleViewController
{
public:
    service_id_t selected_service = 0;
    ImGuiTextFilter services_filter;
    SimpleSlideshowController slideshow_controller;
public:
    SimpleViewController();
    ~SimpleViewController();
    void ClearSearch(void);
};