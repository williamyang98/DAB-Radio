#pragma once

#include <stdint.h>
#include <string>
#include <ctime>
#include <list>

#include "utility/observable.h"

// Forward declare dab_core classes
struct MOT_Entity;
class MOT_Slideshow_Processor;
typedef uint16_t mot_transport_id_t;

// All MOT entities point to buffers which may be overwritten by further entities
// This class copies all the relevant data we need for a slideshow
class Basic_Slideshow_Manager;
enum Basic_Image_Type {
    JPEG, PNG
};

class Basic_Slideshow 
{
public:
    const mot_transport_id_t transport_id;

    Basic_Image_Type image_type;
    uint8_t name_charset = 0;
    std::string name;

    std::time_t trigger_time;
    std::time_t expire_time;

    uint8_t category_id = 0;
    uint8_t slide_id = 0; 
    std::string category_title;
    std::string click_through_url;
    std::string alt_location_url;
    bool is_emergency_alert = false;

    uint8_t* data = NULL;
    int nb_data_bytes = 0;
public:
    Basic_Slideshow(const mot_transport_id_t _id)
    : transport_id(_id) {}
    ~Basic_Slideshow() {
        if (data) delete [] data;
    }
    friend Basic_Slideshow_Manager;
};

class Basic_Slideshow_Manager 
{
private:
    std::list<Basic_Slideshow> slideshows;
    MOT_Slideshow_Processor* slideshow_processor;
    Observable<Basic_Slideshow*> obs_on_new_slideshow;
    Observable<Basic_Slideshow*> obs_on_remove_slideshow;
    int max_size;
public:
    Basic_Slideshow_Manager(const int _max_size=10);
    ~Basic_Slideshow_Manager();
    Basic_Slideshow* Process_MOT_Entity(MOT_Entity* entity);
    auto& GetSlideshows(void) { return slideshows; }
    auto& OnNewSlideshow(void) { return obs_on_new_slideshow; }
    auto& OnRemoveSlideshow(void) { return obs_on_remove_slideshow; }
    void SetMaxSize(const int _max_size);
    int GetMaxSize(void) const { return max_size; };
private:
    void RestrictSize(void);
};
