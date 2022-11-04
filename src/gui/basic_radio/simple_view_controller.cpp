#include "simple_view_controller.h"

Texture* SimpleSlideshowController::AddSlideshow(slideshow_key_t id, const uint8_t* data, const int N) {
    const auto key = GetKey(id);
    auto res = slideshows.find(key);
    if (res == slideshows.end()) {
        res = slideshows.insert({key, std::make_unique<Texture>(data, N)}).first;
    }

    return res->second.get();
}

Texture* SimpleSlideshowController::GetSlideshow(slideshow_key_t id) {
    const auto key = GetKey(id);
    auto res = slideshows.find(key);
    if (res == slideshows.end()) {
        return NULL;
    }
    return res->second.get();
}

uint32_t SimpleSlideshowController::GetKey(slideshow_key_t key) {
    return (key.first << 16) | (key.second);
}

SimpleViewController::SimpleViewController() {

}

SimpleViewController::~SimpleViewController() {

}

void SimpleViewController::ClearSearch(void) {
    services_filter.Clear();
}