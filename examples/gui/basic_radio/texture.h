#pragma once

#include <stdint.h>
#include "utility/span.h"

class Texture
{
private:
    uint32_t m_RendererID;
    int m_Width, m_Height, m_BPP; // BPP = bits per pixel
    bool m_is_success;
public:
    explicit Texture(tcb::span<const uint8_t> image_buffer);
    ~Texture();
    Texture(Texture&) = delete;
    Texture(Texture&&) = delete;
    Texture& operator=(Texture&) = delete;
    Texture& operator=(Texture&&) = delete;
    void* GetTextureID() const;
    inline int GetWidth() const { return m_Width; }
    inline int GetHeight() const { return m_Height; }
};