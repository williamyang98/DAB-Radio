#pragma once

#include <stdint.h>
#include <unordered_map>
#include <memory>
#include <optional>
#include "utility/span.h"
#include "utility/lru_cache.h"
#include "dab/mot/MOT_entities.h"
#include "dab/database/dab_database_entities.h"
#include "./texture.h"

struct Basic_Slideshow;
class BasicRadio;
struct ImGuiTextFilter;

struct SlideshowView {
    subchannel_id_t subchannel_id = 0;
    std::shared_ptr<Basic_Slideshow> slideshow = nullptr;
};

class BasicRadioViewController
{
private:
    LRU_Cache<uint32_t, std::unique_ptr<Texture>> textures;
public:
    std::optional<SlideshowView> selected_slideshow = std::nullopt;
    service_id_t selected_service = 0;
    std::unique_ptr<ImGuiTextFilter> services_filter;
public:
    explicit BasicRadioViewController(const size_t _max_textures=100);
    ~BasicRadioViewController();
    Texture& GetTexture(subchannel_id_t subchannel_id, mot_transport_id_t transport_id, tcb::span<const uint8_t> data);
};