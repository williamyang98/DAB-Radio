#include "texture.h"

#include <assert.h>
#include <iostream>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <GLFW/glfw3.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#include <stb_image.h>

#define GLCall(func) GLClearErrors(#func, __FILE__, __LINE__); func; assert(GLCheckErrors(#func, __FILE__, __LINE__))

void GLClearErrors(const char *funcName, const char *file, int line) {
    while (GLenum error = glGetError()) {
        if (error == GL_NO_ERROR) {
            return;
        }
        // If we don't call it inside a valid opengl context, this error code occurs infinitely
        // Source: https://stackoverflow.com/questions/31462770/glgeterror-returns-1282-infinitely
        // This can occur easily with an imgui app because the context closes before the gui controller is deleted
        if (error == GL_INVALID_OPERATION) {
            return;
        }
        std::cerr << "[OpenGL Error] (" << error << "): " << funcName << "@" << file << ":" << line << std::endl;
    }
}
 
bool GLCheckErrors(const char *funcName, const char *file, int line) {
    while (GLenum error = glGetError()) {
        std::cerr << "[OpenGL Error] (" << error << "): " << funcName << "@" << file << ":" << line << std::endl;
        return false;
    }
    return true; 
}

Texture::Texture(const uint8_t* data, const int N)
    : m_RendererID(0),
      m_LocalBuffer(NULL),
      m_Width(0), m_Height(0), m_BPP(0)
{
    GLCall(glGenTextures(1, &m_RendererID));
    GLCall(glBindTexture(GL_TEXTURE_2D, m_RendererID));

    GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
    GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));

    // wrap texture on x (S) and y (T) axis 
    // NOTE: apparently we need a header file for opengl 1.2
    //       windows doesn't give this at all
    constexpr auto GL_CLAMP_TO_EDGE = 0x812F;
    GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
    GLCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

    // give image buffer to opengl
    // stbi_set_flip_vertically_on_load(1);
    m_LocalBuffer = stbi_load_from_memory(data, N, &m_Width, &m_Height, &m_BPP, 4);
    if (m_LocalBuffer != NULL) {
        GLCall(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_Width, m_Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, m_LocalBuffer));
        // once bound to opengl, free the local buffer
        stbi_image_free(m_LocalBuffer);
    }

    // TODO: how to handle a missing texture?
    GLCall(glBindTexture(GL_TEXTURE_2D, 0));
}

Texture::~Texture()
{
    GLCall(glDeleteTextures(1, &m_RendererID));
}