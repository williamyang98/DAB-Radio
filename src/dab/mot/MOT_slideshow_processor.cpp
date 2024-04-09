#include "./MOT_slideshow_processor.h"
#include <stddef.h>
#include <stdint.h>
#include <fmt/format.h>
#include "utility/span.h"
#include "../dab_logging.h"
#define TAG "mot-slideshow"
static auto _logger = DAB_LOG_REGISTER(TAG);
#define LOG_MESSAGE(...) DAB_LOG_MESSAGE(TAG, fmt::format(__VA_ARGS__))
#define LOG_ERROR(...) DAB_LOG_ERROR(TAG, fmt::format(__VA_ARGS__))

// DOC: ETSI TS 101 499
// Used for all the following code

bool MOT_Slideshow_Processor::ProcessHeaderExtension(MOT_Slideshow& entity, const uint8_t id, tcb::span<const uint8_t> buf) {
    // Clause 6.2.1: General 
    // Table 3: MOT Parameters
    switch (id) {
    case 0x25: return ProcessHeaderExtension_CategoryID_SlideID(entity, buf);
    case 0x26: return ProcessHeaderExtension_CategoryTitle(entity, buf);
    case 0x27: return ProcessHeaderExtension_ClickThroughURL(entity, buf);
    case 0x28: return ProcessHeaderExtension_AlternativeLocationURL(entity, buf);
    case 0x29: return ProcessHeaderExtension_Alert(entity, buf);
    default:
        LOG_ERROR("Unknown param_id={} length={}", id, buf.size());
        return false;
    }
}

bool MOT_Slideshow_Processor::ProcessHeaderExtension_CategoryID_SlideID(MOT_Slideshow& entity, tcb::span<const uint8_t> buf) {
    // Clause 6.2.6: CategoryID/SlideID
    const size_t N = buf.size();
    if (N != 2) {
        LOG_ERROR("Got unexpected length for category/slide id {}!={}", N, 2);
        return false;
    }
    entity.category_id = buf[0];
    entity.slide_id    = buf[1];
    return true;
}

bool MOT_Slideshow_Processor::ProcessHeaderExtension_CategoryTitle(MOT_Slideshow& entity, tcb::span<const uint8_t> buf) {
    // Clause 6.2.7: CategoryTitle
    if (buf.empty()) {
        LOG_ERROR("Got empty buffer for category title");
        return false;
    }
    
    const auto* str_buf = reinterpret_cast<const char*>(buf.data());
    entity.category_title = {str_buf, buf.size()};
    LOG_MESSAGE("Got category_title[{}]={}", buf.size(), entity.category_title);
    return true;
}

bool MOT_Slideshow_Processor::ProcessHeaderExtension_ClickThroughURL(MOT_Slideshow& entity, tcb::span<const uint8_t> buf) {
    // Clause 6.2.8: ClickThroughURL
    if (buf.empty()) {
        LOG_ERROR("Got empty buffer for click through url");
        return false;
    }
    
    const auto* str_buf = reinterpret_cast<const char*>(buf.data());
    entity.click_through_url = {str_buf, buf.size()};
    LOG_MESSAGE("Got click_through_url[{}]={}", buf.size(), entity.click_through_url);
    return true;
}

bool MOT_Slideshow_Processor::ProcessHeaderExtension_AlternativeLocationURL(MOT_Slideshow& entity, tcb::span<const uint8_t> buf) {
    // Clause 6.2.9: AlternativeLocationURL
    if (buf.empty()) {
        LOG_ERROR("Got empty buffer for alt location URL");
        return false;
    }

    const auto* str_buf = reinterpret_cast<const char*>(buf.data());
    entity.alt_location_url = {str_buf, buf.size()};
    LOG_MESSAGE("Got alt_location_url[{}]={}", buf.size(), entity.alt_location_url);
    return true;
}

bool MOT_Slideshow_Processor::ProcessHeaderExtension_Alert(MOT_Slideshow& entity, tcb::span<const uint8_t> buf) {
    // Clause 6.2.10: Alert
    // Table 4: Alert Values
    const int N = (int)buf.size();
    if (N != 1) {
        LOG_ERROR("Got unexpected length for alert {}!={}", N, 1);
        return false;
    }
    const uint8_t alert = buf[0];

    LOG_MESSAGE("Got alert={}", alert);
    switch (alert) {
    case 0x00:
        entity.alert = MOT_Slideshow_Alert::NOT_USED; 
        break;
    case 0x01:
        entity.alert = MOT_Slideshow_Alert::EMERGENCY; 
        break;
    default:
        entity.alert = MOT_Slideshow_Alert::RESERVED_FUTURE_USE; 
        break;
    }
    return true;
}