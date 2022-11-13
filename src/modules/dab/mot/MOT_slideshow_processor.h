#pragma once

#include <stdint.h>

struct MOT_Slideshow;
enum class MOT_Slideshow_Alert {
    NOT_USED, EMERGENCY, RESERVED_FUTURE_USE
};

// Process slideshow specific header extension parameters
class MOT_Slideshow_Processor
{
public:
    // id, buf[N] represents a slideshow specific header extension parameter
    bool ProcessHeaderExtension(MOT_Slideshow* entity, const uint8_t id, const uint8_t* buf, const int N);
private:
    bool ProcessHeaderExtension_CategoryID_SlideID(MOT_Slideshow* entity, const uint8_t* buf, const int N);
    bool ProcessHeaderExtension_CategoryTitle(MOT_Slideshow* entity, const uint8_t* buf, const int N);
    bool ProcessHeaderExtension_ClickThroughURL(MOT_Slideshow* entity, const uint8_t* buf, const int N);
    bool ProcessHeaderExtension_AlternativeLocationURL(MOT_Slideshow* entity, const uint8_t* buf, const int N);
    bool ProcessHeaderExtension_Alert(MOT_Slideshow* entity, const uint8_t* buf, const int N);
};

struct MOT_Slideshow {
    uint8_t category_id = 0;
    uint8_t slide_id = 0;

    struct {
        const char* buf = NULL;
        int nb_bytes = 0;
    } category_title;

    struct {
        const char* buf = NULL;
        int nb_bytes = 0;
    } click_through_url;

    struct {
        const char* buf = NULL;
        int nb_bytes = 0;
    } alt_location_url;

    MOT_Slideshow_Alert alert = MOT_Slideshow_Alert::NOT_USED;
};