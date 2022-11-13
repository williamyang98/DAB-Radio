#include "simple_view_controller.h"

void SimpleViewController::ClearSearch(void) {
    services_filter.Clear();
}

Texture* SimpleViewController::GetTexture(subchannel_id_t subchannel_id, mot_transport_id_t transport_id) {
    const auto key = GetKey(subchannel_id, transport_id);
    auto res = textures.find(key);
    if (res == textures.end()) {
        return NULL;
    }
    return res->second.get();
}

Texture* SimpleViewController::AddTexture(
    subchannel_id_t subchannel_id, mot_transport_id_t transport_id, 
    const uint8_t* data, const int N) 
{
    const auto key = GetKey(subchannel_id, transport_id);
    auto res = textures.find(key);
    if (res == textures.end()) {
        res = textures.insert({key, std::make_unique<Texture>(data, N)}).first;
    }
    return res->second.get();
}

void SimpleViewController::AttachRadio(BasicRadio* radio) {
    radio->On_DAB_Plus_Channel().Attach([this](subchannel_id_t subchannel_id, Basic_DAB_Plus_Channel& channel) {
        auto& slideshow_manager = channel.GetSlideshowManager();
        slideshow_manager.OnRemoveSlideshow().Attach([this](Basic_Slideshow* slideshow) {
            auto selection = GetSelectedSlideshow();
            if (selection.slideshow == slideshow) {
                SetSelectedSlideshow({0, NULL});
            }
        });
    });
}

SelectedSlideshowView SimpleViewController::GetSelectedSlideshow() {
    return selected_slideshow;
}
void SimpleViewController::SetSelectedSlideshow(SelectedSlideshowView _selected_slideshow) {
    selected_slideshow = _selected_slideshow;
}