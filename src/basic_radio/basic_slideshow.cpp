#include "./basic_slideshow.h"
#include <stddef.h>
#include <algorithm>
#include <ctime>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <fmt/format.h>
#include "dab/constants/MOT_content_types.h"
#include "dab/mot/MOT_entities.h"
#include "dab/mot/MOT_slideshow_processor.h"
#include "./basic_radio_logging.h"
#define LOG_MESSAGE(...) BASIC_RADIO_LOG_MESSAGE(fmt::format(__VA_ARGS__))
#define LOG_ERROR(...) BASIC_RADIO_LOG_ERROR(fmt::format(__VA_ARGS__))

static std::time_t Convert_MOT_Time(const MOT_UTC_Time& time) {
    std::tm t;
    t.tm_sec = time.seconds;
    t.tm_min = time.minutes;
    t.tm_hour = time.hours;
    t.tm_year = time.year;
    t.tm_mon = time.month;
    t.tm_mday = time.day;
    return std::mktime(&t);
}

Basic_Slideshow_Manager::Basic_Slideshow_Manager(size_t max_slideshows) {
    m_max_size = max_slideshows;
}

std::shared_ptr<Basic_Slideshow> Basic_Slideshow_Manager::Process_MOT_Entity(MOT_Entity& entity) {
    // DOC: ETSI TS 101 499
    // Clause 6.2.3 MOT ContentTypes and ContentSubTypes 
    // For specific types used for slideshows
    const auto type = entity.header.content_type;
    const auto sub_type = entity.header.content_sub_type;
    const auto mot_type = GetMOTContentType(type, sub_type);

    Basic_Image_Type image_type = Basic_Image_Type::NONE;
    switch (mot_type) {
    case MOT_Content_Subtype::IMAGE_JPEG:
        image_type = Basic_Image_Type::JPEG;
        break;
    case MOT_Content_Subtype::IMAGE_PNG:
        image_type = Basic_Image_Type::PNG;
        break;
    default:
        return nullptr;
    }

    // User application header extension parameters
    auto slideshow = std::make_shared<Basic_Slideshow>();
    slideshow->transport_id = entity.transport_id;
    slideshow->image_type = image_type;

    MOT_Slideshow slideshow_header;
    for (const auto& p: entity.header.user_app_params) {
        MOT_Slideshow_Processor::ProcessHeaderExtension(slideshow_header, p.type, p.data);
    }

    const size_t total_bytes = entity.body_buf.size();
    slideshow->image_data.resize(total_bytes);
    std::copy_n(entity.body_buf.begin(), total_bytes, slideshow->image_data.begin());

    // Core MOT header parameters
    auto& content_name = entity.header.content_name;
    if (content_name.exists) {
        slideshow->name_charset = content_name.charset;
        slideshow->name = std::string(content_name.name);
    }
    auto& expire_time = entity.header.expire_time;
    if (expire_time.exists) {
        slideshow->expire_time = Convert_MOT_Time(expire_time);
    }
    auto& trigger_time = entity.header.trigger_time;
    if (trigger_time.exists) {
        slideshow->trigger_time = Convert_MOT_Time(trigger_time);
    }

    // Slideshow MOT header parameters
    slideshow->category_id = slideshow_header.category_id;
    slideshow->slide_id = slideshow_header.slide_id;

    auto& category_title = slideshow_header.category_title;
    slideshow->category_title = std::string(category_title);

    auto& alt_location_url = slideshow_header.alt_location_url;
    slideshow->alt_location_url = std::string(alt_location_url);

    auto& click_through_url = slideshow_header.click_through_url;
    slideshow->click_through_url = std::string(click_through_url);

    auto& alert = slideshow_header.alert;
    switch (alert) {
    case MOT_Slideshow_Alert::EMERGENCY:
        slideshow->is_emergency_alert = true;
        break;
    case MOT_Slideshow_Alert::NOT_USED:
    case MOT_Slideshow_Alert::RESERVED_FUTURE_USE:
    default:
        slideshow->is_emergency_alert = false;
        break;
    }
 
    {
        auto lock = std::unique_lock(m_mutex_slideshows);
        m_slideshows.push_front(slideshow);
        RestrictSize();
    }

    LOG_MESSAGE("Added slideshow tid={} name={}", slideshow->transport_id, slideshow->name);
    m_obs_on_new_slideshow.Notify(slideshow);
    return slideshow;
}

void Basic_Slideshow_Manager::SetMaxSize(const size_t max_size) {
    auto lock = std::unique_lock(m_mutex_slideshows);
    m_max_size = max_size;
    RestrictSize();
}

void Basic_Slideshow_Manager::RestrictSize(void) {
    if (m_slideshows.size() <= m_max_size) {
        return;
    }
    auto start = m_slideshows.begin();
    auto end = m_slideshows.end();
    std::advance(start, m_max_size);
    m_slideshows.erase(start, end);
}