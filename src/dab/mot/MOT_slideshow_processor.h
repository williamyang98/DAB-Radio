#pragma once

#include <stdint.h>
#include <string_view>
#include "utility/span.h"

struct MOT_Slideshow;

enum class MOT_Slideshow_Alert {
    NOT_USED, EMERGENCY, RESERVED_FUTURE_USE
};

// Process slideshow specific header extension parameters
class MOT_Slideshow_Processor
{
public:
    MOT_Slideshow_Processor() = delete;
    // id, buf[N] represents a slideshow specific header extension parameter
    static bool ProcessHeaderExtension(MOT_Slideshow& entity, const uint8_t id, tcb::span<const uint8_t> buf);
private:
    static bool ProcessHeaderExtension_CategoryID_SlideID(MOT_Slideshow& entity, tcb::span<const uint8_t> buf);
    static bool ProcessHeaderExtension_CategoryTitle(MOT_Slideshow& entity, tcb::span<const uint8_t> buf);
    static bool ProcessHeaderExtension_ClickThroughURL(MOT_Slideshow& entity, tcb::span<const uint8_t> buf);
    static bool ProcessHeaderExtension_AlternativeLocationURL(MOT_Slideshow& entity, tcb::span<const uint8_t> buf);
    static bool ProcessHeaderExtension_Alert(MOT_Slideshow& entity, tcb::span<const uint8_t> buf);
};

struct MOT_Slideshow {
    uint8_t category_id = 0;
    uint8_t slide_id = 0;
    std::string_view category_title;
    std::string_view click_through_url;
    std::string_view alt_location_url;
    MOT_Slideshow_Alert alert = MOT_Slideshow_Alert::NOT_USED;
};