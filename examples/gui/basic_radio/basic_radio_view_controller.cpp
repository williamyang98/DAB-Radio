#include "./basic_radio_view_controller.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <stddef.h>
#include <stdint.h>
#include <memory>
#include "dab/database/dab_database_types.h"
#include "dab/mot/MOT_entities.h"
#include "utility/span.h"
#include "./texture.h"

BasicRadioViewController::BasicRadioViewController(const size_t _max_textures) {
    textures.set_max_size(_max_textures);
    services_filter = std::make_unique<ImGuiTextFilter>();
}

BasicRadioViewController::~BasicRadioViewController() = default;

static uint32_t get_key(subchannel_id_t subchannel_id, mot_transport_id_t transport_id) {
    return (subchannel_id << 16) | transport_id;
}

Texture& BasicRadioViewController::GetTexture(
    subchannel_id_t subchannel_id, mot_transport_id_t transport_id, tcb::span<const uint8_t> data
) {
    const auto key = get_key(subchannel_id, transport_id);
    auto res = textures.find(key);
    if (res == nullptr) {
        auto& texture = textures.emplace(key, std::make_unique<Texture>(data));
        return *(texture.get());
    }
    return *(res->get());
}