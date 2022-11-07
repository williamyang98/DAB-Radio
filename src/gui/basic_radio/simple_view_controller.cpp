#include "simple_view_controller.h"

Texture* TextureTable::AddTexture(slideshow_key_t id, const uint8_t* data, const int N) {
    const auto key = GetKey(id);
    auto res = textures.find(key);
    if (res == textures.end()) {
        res = textures.insert({key, std::make_unique<Texture>(data, N)}).first;
    }

    return res->second.get();
}

Texture* TextureTable::GetTexture(slideshow_key_t id) {
    const auto key = GetKey(id);
    auto res = textures.find(key);
    if (res == textures.end()) {
        return NULL;
    }
    return res->second.get();
}

uint32_t TextureTable::GetKey(slideshow_key_t key) {
    return (key.first << 16) | (key.second);
}

void SimpleViewController::ClearSearch(void) {
    services_filter.Clear();
}

Texture* SimpleViewController::GetTexture(slideshow_key_t key) {
    return texture_table.GetTexture(key);
}

Texture* SimpleViewController::AddTexture(slideshow_key_t key, const uint8_t* data, const int N) {
    return texture_table.AddTexture(key, data, N);
}