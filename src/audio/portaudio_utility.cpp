#include "portaudio_utility.h"

void PaDeviceList::Refresh() {
    const int nb_devices = Pa_GetDeviceCount();
    if (nb_devices < 0) {
        fprintf(stderr, "ERROR: [pa-device-list] Failed to get device count %d\n", nb_devices);
        return;
    }
    devices.clear();
    for (int i = 0; i < nb_devices; i++) {
        const PaDeviceInfo* device_info = Pa_GetDeviceInfo(i);
        if (device_info == NULL) {
            fprintf(stderr, "ERROR: [pa-device-list] Failed to get device info %d\n", i);
            continue;
        }
        if (device_info->maxOutputChannels <= 0) {
            continue;
        }

        // Windows has alot of apis we aren't interested in
        #ifdef _WIN32
        const auto target_host_api_index = Pa_HostApiTypeIdToHostApiIndex(PORTAUDIO_TARGET_HOST_API_ID);
        if (device_info->hostApi != target_host_api_index) {
            continue;
        }
        #endif

        PaDevice device;
        device.label = std::string(device_info->name);
        device.index = i;
        device.host_api_index = device_info->hostApi;
        devices.push_back(device);
    }
}