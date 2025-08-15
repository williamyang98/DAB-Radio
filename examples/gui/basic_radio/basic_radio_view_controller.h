#pragma once

#include <stddef.h>
#include <stdint.h>
#include <memory>
#include <optional>
#include "dab/database/dab_database_entities.h"
#include "dab/mot/MOT_entities.h"
#include "utility/lru_cache.h"
#include "utility/span.h"
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
    std::optional<ServiceId> selected_service = std::nullopt;
    std::unique_ptr<ImGuiTextFilter> services_filter;
public:
    explicit BasicRadioViewController(const size_t _max_textures=100);
    ~BasicRadioViewController();
    Texture& GetTexture(subchannel_id_t subchannel_id, mot_transport_id_t transport_id, tcb::span<const uint8_t> data);
};