#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "getopt/getopt.h"

#include <Windows.h>
#include <io.h>
#include <fcntl.h>

#pragma comment(lib, "winmm.lib")

// Implementation of this pcm player is based off the following source:
// https://blog.csdn.net/weixinhum/article/details/29943973

HANDLE buffer_done_semaphore;

void usage() {
    fprintf(stderr, 
        "pcm_play, plays 16bit pcm file\n\n"
        "Usage:\t[-h (show usage)]\n"
        "\t[-f sample rate (default: 17400Hz)]\n"
        "\t[-b block size (default: 8192)]\n"
        "\t[-c total channels (default: 1)]\n"
        "\t[-e total bits per sample (default: 16)]\n"
    );
}

// https://docs.microsoft.com/en-us/previous-versions/dd743869(v=vs.85)
void CALLBACK wave_callback(
    HWAVEOUT hwo, UINT uMsg, 
    DWORD dwInstance, 
    LPWAVEHDR dwParam1, 
    DWORD dwParam2)
{  
    switch (uMsg)  {
    // https://docs.microsoft.com/en-us/windows/win32/multimedia/wom-done?redirectedfrom=MSDN
    case WOM_DONE:
        {
            // Signal to IO thread that buffer has been read so it can write to it
            ReleaseSemaphore(buffer_done_semaphore, 1, NULL);
            break;  
        }
    }
}

int main(int argc, char** argv) {  
    FILE* fp = stdin;
    // NOTE: Windows does extra translation stuff that messes up the file if this isn't done
    // https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/setmode?view=msvc-170
    _setmode(_fileno(fp), _O_BINARY);

    int Fsample = 87000/5;
    int block_size = 8192;
    int total_channels = 1;
    int total_bits_per_sample = 16;
    
    int opt;
    while ((opt = getopt(argc, argv, "f:b:c:e:h")) != -1) {
        switch (opt) {
        case 'f':
            Fsample = static_cast<int>(atof(optarg));
            break;
        case 'b':
            block_size = static_cast<int>(atof(optarg));
            break;
        case 'c':
            total_channels = static_cast<int>(atof(optarg));
            break;
        case 'e':
            total_bits_per_sample = static_cast<int>(atof(optarg));
            break;
        case 'h':
        case '?':
            usage();
            return 0;
        }
    }

    if (total_channels < 0) {
        fprintf(stderr, "Number of channels must be a positive number (%d)\n", total_channels);
        return 1;
    }

    if (total_bits_per_sample < 0) {
        fprintf(stderr, "Bits per sample must be a positive number (%d)\n", total_channels);
        return 1;
    }

    if ((total_bits_per_sample % 8) != 0) {
        fprintf(stderr, "Bits per sample must be a multiple of 8 bits (%d)\n", total_channels);
        return 1;
    }

    if (Fsample <= 0) {
        fprintf(stderr, "Sample rate must be a positive number (%d)\n", Fsample);
        return 1;
    }

    if (block_size <= 0) {
        fprintf(stderr, "Block size must be a positive number (%d)\n", block_size);
        return 1;
    }

    // setup semaphore for signalling between audio thread and io thread
    // Start with an initial value of 1 so the double buffer always has at least one buffer written
    buffer_done_semaphore = CreateSemaphore(NULL, 1, 1, NULL);
    if (buffer_done_semaphore == NULL) {
        fprintf(stderr, "Failed to create buffer complete sempahore\n");
        return 1;
    }

    const int total_bytes_per_sample = total_bits_per_sample/8;

    // setup win api sound
    WAVEFORMATEX wave_format;
    wave_format.wFormatTag = WAVE_FORMAT_PCM;
    wave_format.nChannels = total_channels;
    wave_format.nSamplesPerSec = Fsample;
    wave_format.nAvgBytesPerSec = Fsample*total_bytes_per_sample;
    wave_format.wBitsPerSample = total_bits_per_sample;
    wave_format.nBlockAlign = total_bytes_per_sample*total_channels;
    wave_format.cbSize = 0;

    HWAVEOUT wave_out;
    waveOutOpen(&wave_out, WAVE_MAPPER, &wave_format, (DWORD_PTR)wave_callback, 0L, CALLBACK_FUNCTION);

    WAVEHDR wave_header_1;
    wave_header_1.lpData = new char[block_size];  
    wave_header_1.dwBufferLength = block_size;
    wave_header_1.dwLoops = 0;
    wave_header_1.dwFlags = 0;

    WAVEHDR wave_header_2;
    wave_header_2.lpData = new char[block_size];  
    wave_header_2.dwBufferLength = block_size;
    wave_header_2.dwLoops = 0;
    wave_header_2.dwFlags = 0;

    WAVEHDR* active_buffer = &wave_header_1;
    WAVEHDR* inactive_buffer = &wave_header_2;

    while (true) {
        while (true) {
            size_t nb_read = fread(inactive_buffer->lpData, 1, block_size, fp);
            if (nb_read == block_size) {
                break;
            }
            fprintf(stderr, "Expected %d samples got %d at buffer %p eof=%d\n", block_size, static_cast<int>(nb_read), inactive_buffer->lpData, feof(fp));
            Sleep(10);
            return 0;
        }

        // Write to the inactive buffer, and wait for the active buffer to be processed
        inactive_buffer->dwBufferLength = block_size;
        waveOutPrepareHeader(wave_out, inactive_buffer, sizeof(WAVEHDR));  
        waveOutWrite(wave_out, inactive_buffer, sizeof(WAVEHDR));  

        // Wait for active buffer to be complete and swap
        WaitForSingleObject(buffer_done_semaphore, 1000);

        // Swap double buffers 
        WAVEHDR* tmp = active_buffer;
        active_buffer = inactive_buffer;
        inactive_buffer = tmp;
    }
  
    waveOutUnprepareHeader(wave_out, &wave_header_1, sizeof(WAVEHDR));
    waveOutUnprepareHeader(wave_out, &wave_header_2, sizeof(WAVEHDR));  
    
    CloseHandle(buffer_done_semaphore);

    delete [] wave_header_1.lpData;  
    delete [] wave_header_2.lpData;  

    fclose(fp);
    return 0;  
}
