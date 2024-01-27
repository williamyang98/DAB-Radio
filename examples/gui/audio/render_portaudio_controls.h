#pragma once

#include <portaudio.h>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include "audio/portaudio_sink.h"
#include "audio/audio_pipeline.h"

class ThreadedRunner
{
private:
    bool m_is_alive = true;
    bool m_is_trigger = false;
    std::mutex m_mutex_trigger;
    std::mutex m_mutex_action;
    std::condition_variable m_cv_trigger;
    std::function<void()> m_action = nullptr;
    std::unique_ptr<std::thread> m_thread = nullptr;
public:
    ThreadedRunner() {
        m_thread = std::make_unique<std::thread>([this]() {
            while (true)  {
                {
                    auto lock_trigger = std::unique_lock(m_mutex_trigger);
                    m_cv_trigger.wait(lock_trigger, [this]() {
                        return m_is_trigger || !m_is_alive;
                    });
                    if (!m_is_alive) return;
                }
                {
                    auto lock_action = std::scoped_lock(m_mutex_action);
                    if (m_action != nullptr) m_action();
                }
                {
                    auto lock_trigger = std::scoped_lock(m_mutex_trigger);
                    m_is_trigger = false;
                }
            }
        });
    }
    ~ThreadedRunner() {
        {
            auto lock_trigger = std::scoped_lock(m_mutex_trigger);
            m_is_alive = false;
        }
        m_cv_trigger.notify_one();
        m_thread->join();
    }
    template <typename F>
    bool submit(F&& action) {
        {
            auto lock_trigger = std::scoped_lock(m_mutex_trigger);
            if (m_is_trigger) return false;

            auto lock_action = std::scoped_lock(m_mutex_action);
            m_action = std::move(action);
            m_is_trigger = true;
        }
        m_cv_trigger.notify_one();
        return true;
    }
};

// portaudio api calls are blocking so we run them in a separate thread to avoid UI stutter
class PortAudioThreadedActions 
{
private:
    ThreadedRunner m_runner_refresh;
    ThreadedRunner m_runner_select;
    std::vector<PortAudioDevice> m_devices;
    std::mutex m_mutex_devices;
    bool m_is_refresh_pending = false;
    bool m_is_select_pending = false;
public:
    auto& get_devices_mutex() { return m_mutex_devices; }
    tcb::span<const PortAudioDevice> get_devices() const { return m_devices; }
    bool refresh() { 
        return m_runner_refresh.submit([this]() {
            m_is_refresh_pending = true;
            // TODO: portaudio has a WIP hotplugging
            //       https://github.com/PortAudio/portaudio/wiki/HotPlug
            // Pa_RefreshDeviceList();
            auto devices = get_portaudio_devices();
            auto lock = std::scoped_lock(m_mutex_devices);
            m_devices = std::move(devices);
            m_is_refresh_pending = false;
        }); 
    }
    bool select_device(PaDeviceIndex device_index, std::shared_ptr<AudioPipeline> pipeline) {
        return m_runner_select.submit([pipeline, device_index, this]() {
            m_is_select_pending = true;
            auto sink_res = PortAudioSink::create_from_index(device_index);
            if (sink_res.error == PortAudioSinkCreateError::SUCCESS) {
                pipeline->set_sink(std::move(sink_res.sink));
            }
            m_is_select_pending = false;
        });
    }
    bool get_is_refresh_pending() const { return m_is_refresh_pending; }
    bool get_is_select_pending() const { return m_is_select_pending; }
};

void RenderPortAudioControls(PortAudioThreadedActions& actions, std::shared_ptr<AudioPipeline> pipeline);
void RenderVolumeSlider(float& volume_gain);