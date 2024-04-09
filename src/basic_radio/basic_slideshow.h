#pragma once

#include <stdint.h>
#include <string>
#include <list>
#include <vector>
#include <memory>
#include <ctime>
#include <mutex>

#include "dab/mot/MOT_entities.h"
#include "utility/observable.h"

enum class Basic_Image_Type {
    NONE, JPEG, PNG
};

struct Basic_Slideshow {
    mot_transport_id_t transport_id;
    Basic_Image_Type image_type = Basic_Image_Type::NONE;
    uint8_t name_charset = 0;
    std::string name;
    std::time_t trigger_time = 0;
    std::time_t expire_time = 0;
    uint8_t category_id = 0;
    uint8_t slide_id = 0; 
    std::string category_title = "";
    std::string click_through_url = "";
    std::string alt_location_url = "";
    bool is_emergency_alert = false;
    std::vector<uint8_t> image_data;
};

class Basic_Slideshow_Manager 
{
private:
    std::list<std::shared_ptr<Basic_Slideshow>> m_slideshows;
    Observable<std::shared_ptr<Basic_Slideshow>> m_obs_on_new_slideshow;
    size_t m_max_size;
    std::mutex m_mutex_slideshows;
public:
    explicit Basic_Slideshow_Manager(size_t max_slideshows=25);
    // returns nullptr if MOT entity wasn't a slideshow
    std::shared_ptr<Basic_Slideshow> Process_MOT_Entity(MOT_Entity& entity);
    auto& GetSlideshowsMutex(void) { return m_mutex_slideshows; }
    auto& GetSlideshows(void) { return m_slideshows; }
    auto& OnNewSlideshow(void) { return m_obs_on_new_slideshow; }
    void SetMaxSize(const size_t max_size);
    size_t GetMaxSize(void) const { return m_max_size; };
private:
    void RestrictSize(void);
};
