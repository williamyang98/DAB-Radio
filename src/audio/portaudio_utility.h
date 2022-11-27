#pragma once

#include <portaudio.h>
#include <string>
#include <vector>

#ifdef _WIN32
constexpr PaHostApiTypeId PORTAUDIO_TARGET_HOST_API_ID = PaHostApiTypeId::paDirectSound;
#endif

class ScopedPaHandler
{
private:
    PaError _result;
public:
    ScopedPaHandler(): _result(Pa_Initialize()) {}
    ~ScopedPaHandler() {
        if (_result == paNoError) {
            Pa_Terminate();
        }
    }
    PaError result() const { return _result; }
};

struct PaDevice {
    PaDeviceIndex index;
    PaHostApiIndex host_api_index;
    std::string label;
};


class PaDeviceList 
{
private:
    std::vector<PaDevice> devices;    
public:
    PaDeviceList() {
        Refresh();
    }
    void Refresh(); 
    auto& GetDevices() { return devices; }    
};