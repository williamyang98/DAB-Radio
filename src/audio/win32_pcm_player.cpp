#include "win32_pcm_player.h"

#include <Windows.h>

#pragma comment(lib, "winmm.lib")

// Implementation of this pcm player is based off the following source:
// https://blog.csdn.net/weixinhum/article/details/29943973

// https://docs.microsoft.com/en-us/previous-versions/dd743869(v=vs.85)
void CALLBACK wave_callback(
    HWAVEOUT  hwo,
    UINT      uMsg,
    DWORD_PTR dwInstance,
    DWORD_PTR dwParam1,
    DWORD_PTR dwParam2);

class Win32_PCM_Player::Win32Params 
{
public:
    WAVEFORMATEX wave_format;
    HWAVEOUT wave_out;
    WAVEHDR wave_header_0;
    WAVEHDR wave_header_1;
    WAVEHDR* active_wave_header;
    WAVEHDR* inactive_wave_header;
    HANDLE buffer_done_semaphore;
public:
    Win32Params(const uint8_t nb_channels, const uint32_t Fsample, const uint8_t nb_bytes_per_sample) {
        buffer_done_semaphore = CreateSemaphore(NULL, 1, 1, NULL);

        const uint8_t nb_bits_per_sample = nb_bytes_per_sample * 8;

        wave_format.wFormatTag = WAVE_FORMAT_PCM;
        wave_format.nChannels = nb_channels;
        wave_format.nSamplesPerSec = Fsample;
        wave_format.nAvgBytesPerSec = Fsample*nb_bytes_per_sample*nb_channels;
        wave_format.wBitsPerSample = nb_bits_per_sample;
        wave_format.nBlockAlign = nb_bytes_per_sample*nb_channels;
        wave_format.cbSize = 0;

        waveOutOpen(&wave_out, WAVE_MAPPER, &wave_format, (DWORD_PTR)wave_callback, reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION);

        wave_header_0.lpData = NULL;  
        wave_header_0.dwBufferLength = 0;
        wave_header_0.dwLoops = 0;
        wave_header_0.dwFlags = 0;

        wave_header_1.lpData = NULL;  
        wave_header_1.dwBufferLength = 0;
        wave_header_1.dwLoops = 0;
        wave_header_1.dwFlags = 0;

        active_wave_header = &wave_header_0;
        inactive_wave_header = &wave_header_1;
    }
    ~Win32Params() {
        WaitForSingleObject(buffer_done_semaphore, 1000);
        waveOutUnprepareHeader(wave_out, &wave_header_0, sizeof(WAVEHDR));
        waveOutUnprepareHeader(wave_out, &wave_header_1, sizeof(WAVEHDR));  
        waveOutClose(wave_out);
    }
    void SwapHeaders() {
        auto tmp = active_wave_header;
        active_wave_header = inactive_wave_header;
        inactive_wave_header = tmp;
    }
    void AssociateBuffers(uint8_t* buf0, uint8_t* buf1, const int N) {
        wave_header_0.lpData = reinterpret_cast<char*>(buf0);
        wave_header_1.lpData = reinterpret_cast<char*>(buf1);
        wave_header_0.dwBufferLength = N;
        wave_header_1.dwBufferLength = N;
    }
};

void CALLBACK wave_callback(
    HWAVEOUT  hwo,
    UINT      uMsg,
    DWORD_PTR dwInstance,
    DWORD_PTR dwParam1,
    DWORD_PTR dwParam2)
{  
    // We can get a reference to the pcm player that is associated to this wave player
    auto pcm_player = reinterpret_cast<Win32_PCM_Player::Win32Params*>(dwInstance);
    switch (uMsg)  {
    // https://docs.microsoft.com/en-us/windows/win32/multimedia/wom-done?redirectedfrom=MSDN
    case WOM_DONE:
        {
            // Signal to IO thread that buffer has been read so it can write to it
            ReleaseSemaphore(pcm_player->buffer_done_semaphore, 1, NULL);
            break;  
        }
    }
}

Win32_PCM_Player::Win32_PCM_Player() {
    params.block_size = 48000;
    params.bytes_per_sample = 2;
    params.sample_rate = 48000;
    params.total_channels = 2;

    block_buf_0.resize(params.block_size, 0);
    block_buf_1.resize(params.block_size, 0);

    wave_data = std::make_unique<Win32Params>(params.total_channels, params.sample_rate, params.bytes_per_sample);
    wave_data->AssociateBuffers(block_buf_0.data(), block_buf_1.data(), params.block_size);

    is_running = true;
    audio_thread = std::make_unique<std::thread>([this]() {
        RunnerThread();
    });

    // prime the double buffer
    auto lock_rx = std::unique_lock(mutex_cv_received_block);
    is_received_block = true;
    cv_received_block.notify_one();
}

Win32_PCM_Player::~Win32_PCM_Player() {
    is_running = false;
    audio_thread->join();
}

bool Win32_PCM_Player::SetParameters(const Parameters new_params) {
    if (params == new_params) {
        return false;
    }

    Regenerate(new_params);
    return true;
}

Win32_PCM_Player::Parameters Win32_PCM_Player::GetParameters(void) {
    return params;
}

void Win32_PCM_Player::Regenerate(const Parameters new_params) {
    auto lock_rx = std::scoped_lock(mutex_cv_received_block);
    auto lock_tx = std::scoped_lock(mutex_cv_free_block);

    // Reallocate input buffers
    if (params.block_size != new_params.block_size) {
        block_buf_0.resize(new_params.block_size);
        block_buf_1.resize(new_params.block_size);
        inactive_block_nb_bytes = 0;
    }

    // if the playback parameters changed
    const bool is_changed = 
        (params.sample_rate != new_params.sample_rate) ||
        (params.bytes_per_sample != new_params.bytes_per_sample) ||
        (params.total_channels != new_params.total_channels);

    if (is_changed) {
        wave_data = std::make_unique<Win32Params>(
            new_params.total_channels, 
            new_params.sample_rate, 
            new_params.bytes_per_sample);
    }

    wave_data->AssociateBuffers(block_buf_0.data(), block_buf_1.data(), new_params.block_size);

    // Restart the audio player
    // prime the double buffer
    params = new_params;
    is_received_block = true;
    cv_received_block.notify_one();
}

// Block until buffer is consumed
void Win32_PCM_Player::ConsumeBuffer(tcb::span<const uint8_t> buf) {
    const int N = (int)buf.size();
    int curr_byte = 0;
    while (curr_byte < N) {
        bool is_block_full = false;
        {
            // wait until we can write data
            auto lock_tx = std::unique_lock(mutex_cv_free_block);
            cv_free_block.wait(lock_tx, [this]() { return is_free_block; });

            const int nb_required = (params.block_size-inactive_block_nb_bytes);
            const int nb_remain = N-curr_byte;
            const int nb_push = (nb_required > nb_remain) ? nb_remain : nb_required;

            auto audio_buf = reinterpret_cast<uint8_t*>(
                &wave_data->inactive_wave_header->lpData[inactive_block_nb_bytes]);

            for (int i = 0; i < nb_push; i++) {
                audio_buf[i] = buf[curr_byte+i];
            }

            inactive_block_nb_bytes += nb_push;
            curr_byte += nb_push;

            is_block_full = inactive_block_nb_bytes == params.block_size;
            is_free_block = !is_block_full;
        }

        if (is_block_full) {
            // transmit the block
            auto lock_rx = std::scoped_lock(mutex_cv_received_block);
            is_received_block = true;
            cv_received_block.notify_one();
        } 
    }
}


void Win32_PCM_Player::RunnerThread() {
    while (is_running) {
        {
            // Wait for the secondary buffer to be filled
            auto lock_rx = std::unique_lock(mutex_cv_received_block);
            cv_received_block.wait(lock_rx, [this]() { return is_received_block; });
            is_received_block = false;

            if (!is_running) {
                return;
            }

            // Write to the inactive buffer, and wait for the active buffer to be processed
            wave_data->inactive_wave_header->dwBufferLength = params.block_size;

            waveOutPrepareHeader(
                wave_data->wave_out, 
                wave_data->inactive_wave_header, sizeof(WAVEHDR));  

            waveOutWrite(
                wave_data->wave_out, 
                wave_data->inactive_wave_header, sizeof(WAVEHDR));  

            // We can switch to the other buffer once it has been played
            WaitForSingleObject(wave_data->buffer_done_semaphore, 1000);
        }
        {
            auto lock_tx = std::scoped_lock(mutex_cv_free_block);
            wave_data->SwapHeaders();
            // Swap double buffers 
            inactive_block_nb_bytes = 0;
            is_free_block = true;
            cv_free_block.notify_one();
        }
    }
}