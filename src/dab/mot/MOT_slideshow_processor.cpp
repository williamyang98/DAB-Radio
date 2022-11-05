#include "MOT_slideshow_processor.h"

#include "easylogging++.h"
#include "fmt/core.h"

#define LOG_MESSAGE(...) CLOG(INFO, "mot-slideshow") << fmt::format(__VA_ARGS__)
#define LOG_ERROR(...) CLOG(ERROR, "mot-slideshow") << fmt::format(__VA_ARGS__)

// DOC: ETSI TS 101 499
// Used for all the following code

bool MOT_Slideshow_Processor::ProcessHeaderExtension(MOT_Slideshow* entity, const uint8_t id, const uint8_t* buf, const int N) {
    // Clause 6.2.1: General 
    // Table 3: MOT Parameters
    switch (id) {
    case 0x25: return ProcessHeaderExtension_CategoryID_SlideID(entity, buf, N);
    case 0x26: return ProcessHeaderExtension_CategoryTitle(entity, buf, N);
    case 0x27: return ProcessHeaderExtension_ClickThroughURL(entity, buf, N);
    case 0x28: return ProcessHeaderExtension_AlternativeLocationURL(entity, buf, N);
    case 0x29: return ProcessHeaderExtension_Alert(entity, buf, N);
    default:
        LOG_ERROR("Unknown param_id={} length={}", id, N);
        return false;
    }
}

bool MOT_Slideshow_Processor::ProcessHeaderExtension_CategoryID_SlideID(MOT_Slideshow* entity, const uint8_t* buf, const int N) {
    // Clause 6.2.6: CategoryID/SlideID
    if (N != 2) {
        LOG_ERROR("Got unexpected length for category/slide id {}!={}", N, 2);
        return false;
    }
    entity->category_id = buf[0];
    entity->slide_id    = buf[1];
    return true;
}

bool MOT_Slideshow_Processor::ProcessHeaderExtension_CategoryTitle(MOT_Slideshow* entity, const uint8_t* buf, const int N) {
    // Clause 6.2.7: CategoryTitle
    if (N <= 0) {
        LOG_ERROR("Got empty buffer for category title {}<=0", N);
        return false;
    }
    
    const auto* str_buf = reinterpret_cast<const char*>(buf);
    entity->category_title.buf = str_buf;
    entity->category_title.nb_bytes = N;
    LOG_MESSAGE("Got category_title[{}]={:s}", N, str_buf);
    return true;
}

bool MOT_Slideshow_Processor::ProcessHeaderExtension_ClickThroughURL(MOT_Slideshow* entity, const uint8_t* buf, const int N) {
    // Clause 6.2.8: ClickThroughURL
    if (N <= 0) {
        LOG_ERROR("Got empty buffer for click through url {}<=0", N);
        return false;
    }
    
    const auto* str_buf = reinterpret_cast<const char*>(buf);
    entity->click_through_url.buf = str_buf;
    entity->click_through_url.nb_bytes = N;
    LOG_MESSAGE("Got click_through_url[{}]={:s}", N, str_buf);
    return true;
}

bool MOT_Slideshow_Processor::ProcessHeaderExtension_AlternativeLocationURL(MOT_Slideshow* entity, const uint8_t* buf, const int N) {
    // Clause 6.2.9: AlternativeLocationURL
    if (N <= 0) {
        LOG_ERROR("Got empty buffer for alt location URL{}<=0", N);
        return false;
    }

    const auto* str_buf = reinterpret_cast<const char*>(buf);
    entity->alt_location_url.buf = str_buf;
    entity->alt_location_url.nb_bytes = N;
    LOG_MESSAGE("Got alt_location_url[{}]={:s}", N, N, str_buf);
    return true;
}

bool MOT_Slideshow_Processor::ProcessHeaderExtension_Alert(MOT_Slideshow* entity, const uint8_t* buf, const int N) {
    // Clause 6.2.10: Alert
    // Table 4: Alert Values
    if (N != 1) {
        LOG_ERROR("Got unexpected length for alert {}!={}", N, 1);
        return false;
    }
    const uint8_t alert = buf[0];

    LOG_MESSAGE("Got alert={}", alert);
    switch (alert) {
    case 0x00:
        entity->alert = MOT_Slideshow_Alert::NOT_USED; 
        break;
    case 0x01:
        entity->alert = MOT_Slideshow_Alert::EMERGENCY; 
        break;
    default:
        entity->alert = MOT_Slideshow_Alert::RESERVED_FUTURE_USE; 
        break;
    }
    return true;
}