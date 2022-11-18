#include "simple_view_controller.h"

#include "modules/basic_radio/basic_radio.h"
#include "modules/basic_radio/basic_slideshow.h"

SimpleViewController::SimpleViewController(BasicRadio& _radio, const int _max_textures) 
: radio(_radio), max_textures(_max_textures)
{
    // Be generous and assume we will render up to 3 times the minimum number of textures
    textures.set_max_size(max_textures*3);

    radio.On_DAB_Plus_Channel().Attach([this](subchannel_id_t subchannel_id, Basic_DAB_Plus_Channel& channel) {
        auto& slideshow_manager = channel.GetSlideshowManager();
        slideshow_manager.SetMaxSize(max_textures);
        slideshow_manager.OnRemoveSlideshow().Attach([this](Basic_Slideshow& slideshow) {
            auto selection = GetSelectedSlideshow();
            if (selection.slideshow == (&slideshow)) {
                SetSelectedSlideshow({0, NULL});
            }
        });
    });
}

SimpleViewController::~SimpleViewController() = default;

void SimpleViewController::ClearSearch(void) {
    services_filter.Clear();
}

Texture* SimpleViewController::GetTexture(subchannel_id_t subchannel_id, mot_transport_id_t transport_id) {
    const auto key = GetKey(subchannel_id, transport_id);
    auto res = textures.find(key);
    if (res == NULL) {
        return NULL;
    }
    return res->get();
}

Texture* SimpleViewController::AddTexture(
    subchannel_id_t subchannel_id, 
    mot_transport_id_t transport_id, 
    tcb::span<const uint8_t> data) 
{
    const auto key = GetKey(subchannel_id, transport_id);
    auto res = textures.find(key);
    if (res == NULL) {
        // res = textures.insert({key, std::move(std::make_unique<Texture>(data))}).first;
        // auto& v = textures.insert(key, std::move(std::make_unique<Texture>(data)));
        auto& v = textures.emplace(key, std::move(std::make_unique<Texture>(data)));
        return v.get();
    }
    return res->get();
}

SelectedSlideshowView SimpleViewController::GetSelectedSlideshow() {
    return selected_slideshow;
}
void SimpleViewController::SetSelectedSlideshow(SelectedSlideshowView _selected_slideshow) {
    selected_slideshow = _selected_slideshow;
}