#pragma once

#include "pcm_player.h"
#include "utility/span.h"

#include <vector>
#include <thread>
#include <memory>
#include <mutex>
#include <condition_variable>

class Win32_PCM_Player: public PCM_Player 
{
public:
    // Avoid win32 type declaration since windows.h header is gigantic
    class Win32Params;
private:
    Parameters params;
    bool is_running = true;
    // double buffering
    std::vector<uint8_t> block_buf_0;
    std::vector<uint8_t> block_buf_1;
    // Win32 wave data
    std::unique_ptr<Win32Params> wave_data;
    // Separate thread for audio running
    std::unique_ptr<std::thread> audio_thread;

    std::mutex mutex_cv_received_block;
    std::condition_variable cv_received_block;
    bool is_received_block = false;

    std::mutex mutex_cv_free_block;
    std::condition_variable cv_free_block;
    bool is_free_block = false;

    // Keep track of how many bytes we have left to write
    int inactive_block_nb_bytes = 0;
public:
    Win32_PCM_Player();
    ~Win32_PCM_Player();
    // We bind a callback through the win32 api, so we cannot move the location of this object
    Win32_PCM_Player(Win32_PCM_Player&) = delete;
    Win32_PCM_Player(Win32_PCM_Player&&) = delete;
    Win32_PCM_Player& operator=(Win32_PCM_Player&) = delete;
    Win32_PCM_Player& operator=(Win32_PCM_Player&&) = delete;

    // Block until buffer is consumed
    virtual void ConsumeBuffer(tcb::span<const uint8_t> buf);
    virtual bool SetParameters(const Parameters new_params);
    virtual Parameters GetParameters(void);
private:
    // Separate thread to process the audio block
    void RunnerThread();
    // Update internal handlers when parameters changed
    void Regenerate(const Parameters new_params);
};