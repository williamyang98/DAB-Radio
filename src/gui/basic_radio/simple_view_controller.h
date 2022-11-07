#pragma once

#include <stdint.h>
#include <unordered_map>
#include <memory>

#include "imgui.h"
#include "texture.h"

#include "dab/database/dab_database_entities.h"
#include "basic_radio/basic_slideshow.h"

using slideshow_key_t = std::pair<subchannel_id_t, mot_transport_id_t>;

class TextureTable 
{
public:
private:
    std::unordered_map<uint32_t, std::unique_ptr<Texture>> textures;
public:
    Texture* AddTexture(slideshow_key_t id, const uint8_t* data, const int N);
    Texture* GetTexture(slideshow_key_t id);
private:
    uint32_t GetKey(slideshow_key_t key);
};

struct SelectedSlideshowView {
    subchannel_id_t subchannel_id = 0;
    mot_transport_id_t transport_id = 0;
    Basic_Slideshow* slideshow = NULL;
};

class SimpleViewController
{
private:
    TextureTable texture_table;
    SelectedSlideshowView selected_slideshow;
public:
    service_id_t selected_service = 0;
    ImGuiTextFilter services_filter;
public:
    void ClearSearch(void);
    Texture* GetTexture(slideshow_key_t key);
    Texture* AddTexture(slideshow_key_t key, const uint8_t* data, const int N);
    SelectedSlideshowView GetSelectedSlideshow() {
        return selected_slideshow;
    }
    void SetSelectedSlideshow(SelectedSlideshowView _selected_slideshow) {
        selected_slideshow = _selected_slideshow;
    }
};