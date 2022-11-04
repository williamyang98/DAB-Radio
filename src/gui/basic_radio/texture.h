#pragma once

#include <stdint.h>

class Texture
{
private:
    uint32_t m_RendererID;
    uint8_t* m_LocalBuffer;
    int m_Width, m_Height, m_BPP; // BPP = bits per pixel
public:
    Texture(const uint8_t* data, const int N);
    ~Texture();
    uint32_t GetTextureID() const { return m_RendererID; }
    inline int GetWidth() const { return m_Width; }
    inline int GetHeight() const { return m_Height; }
};