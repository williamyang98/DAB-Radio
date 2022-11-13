#include "basic_slideshow.h"

#include "modules/dab/constants/MOT_content_types.h"
#include "modules/dab/mot/MOT_slideshow_processor.h"
#include "modules/dab/mot/MOT_processor.h"

#include "easylogging++.h"
#include "fmt/core.h"

#define LOG_MESSAGE(...) CLOG(INFO, "basic-radio") << fmt::format(__VA_ARGS__)
#define LOG_ERROR(...) CLOG(ERROR, "basic-radio") << fmt::format(__VA_ARGS__)

static std::time_t Convert_MOT_Time(MOT_UTC_Time& time) {
    std::tm t;
    t.tm_sec = time.seconds;
    t.tm_min = time.minutes;
    t.tm_hour = time.hours;
    t.tm_year = time.year;
    t.tm_mon = time.month;
    t.tm_mday = time.day;
    return std::mktime(&t);
}

Basic_Slideshow_Manager::Basic_Slideshow_Manager(const int _max_size) {
    slideshow_processor = new MOT_Slideshow_Processor();
    SetMaxSize(_max_size);
}

Basic_Slideshow_Manager::~Basic_Slideshow_Manager() {
    delete slideshow_processor;
}

// Returns NULL if this entity isn't a slideshow
// Returns pointer to Basic_Slideshow if it was created or already exists
Basic_Slideshow* Basic_Slideshow_Manager::Process_MOT_Entity(MOT_Entity* entity) {
    // DOC: ETSI TS 101 499
    // Clause 6.2.3 MOT ContentTypes and ContentSubTypes 
    // For specific types used for slideshows
    const auto type = entity->header.content_type;
    const auto sub_type = entity->header.content_sub_type;
    const auto mot_type = GetMOTContentType(type, sub_type);

    Basic_Image_Type image_type;
    switch (mot_type) {
    case MOT_Content_Subtype::IMAGE_JPEG:
        image_type = Basic_Image_Type::JPEG;
        break;
    case MOT_Content_Subtype::IMAGE_PNG:
        image_type = Basic_Image_Type::PNG;
        break;
    default:
        return NULL;
    }

    // User application header extension parameters
    MOT_Slideshow slideshow_header;
    for (auto& p: entity->header.user_app_params) {
        slideshow_processor->ProcessHeaderExtension(
            &slideshow_header, 
            p.type, p.data, p.nb_data_bytes);
    }

    slideshows.emplace_front(entity->transport_id);
    RestrictSize();

    auto& slideshow = slideshows.front();
    slideshow.image_type = image_type;

    const int N = entity->nb_body_bytes;
    slideshow.data = new uint8_t[N];
    slideshow.nb_data_bytes = N;
    for (int i = 0; i < N; i++) {
        slideshow.data[i] = entity->body_buf[i];
    }

    // Core MOT header parameters
    auto& content_name = entity->header.content_name;
    if (content_name.exists) {
        slideshow.name_charset = content_name.charset;
        slideshow.name = std::string(content_name.name, content_name.nb_bytes);
    }
    auto& expire_time = entity->header.expire_time;
    if (expire_time.exists) {
        slideshow.expire_time = Convert_MOT_Time(expire_time);
    }
    auto& trigger_time = entity->header.trigger_time;
    if (trigger_time.exists) {
        slideshow.trigger_time = Convert_MOT_Time(trigger_time);
    }

    // Slideshow MOT header parameters
    slideshow.category_id = slideshow_header.category_id;
    slideshow.slide_id = slideshow_header.slide_id;
    auto& category_title = slideshow_header.category_title;
    if (category_title.buf != NULL) {
        slideshow.category_title = std::string(
            category_title.buf, category_title.nb_bytes);
    }
    auto& alt_location_url = slideshow_header.alt_location_url;
    if (alt_location_url.buf != NULL) {
        slideshow.alt_location_url = std::string(
            alt_location_url.buf, alt_location_url.nb_bytes);
    }
    auto& click_through_url = slideshow_header.click_through_url;
    if (click_through_url.buf != NULL) {
        slideshow.click_through_url = std::string(
            click_through_url.buf, click_through_url.nb_bytes);
    }
    auto& alert = slideshow_header.alert;
    switch (alert) {
    case MOT_Slideshow_Alert::EMERGENCY:
        slideshow.is_emergency_alert = true;
        break;
    case MOT_Slideshow_Alert::NOT_USED:
    case MOT_Slideshow_Alert::RESERVED_FUTURE_USE:
    default:
        slideshow.is_emergency_alert = false;
        break;
    }

    LOG_MESSAGE("Added slideshow tid={} name={}", slideshow.transport_id, slideshow.name);
    obs_on_new_slideshow.Notify(&slideshow);
    return &slideshow;
}

void Basic_Slideshow_Manager::SetMaxSize(const int _max_size) {
    max_size = _max_size;
    RestrictSize();
}

void Basic_Slideshow_Manager::RestrictSize(void) {
    if (slideshows.size() <= max_size) {
        return;
    }

    auto start = slideshows.begin();
    auto end = slideshows.end();
    std::advance(start, max_size);

    for (auto it = start; it != end; it++) {
        auto* v = &(*it);
        obs_on_remove_slideshow.Notify(v);
    }

    slideshows.erase(start, end);
}