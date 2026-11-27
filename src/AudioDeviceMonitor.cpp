#include "AudioDeviceMonitor.h"

#include <portaudio.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <utility>
#include <vector>

AudioDeviceMonitor::AudioDeviceMonitor() {
    initializeDevices();
}

AudioDeviceMonitor::~AudioDeviceMonitor() {
    stop();
    if (portaudio_initialized_) {
        Pa_Terminate();
    }
}

void AudioDeviceMonitor::start() {
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    if (running_.exchange(true)) {
        return;
    }

    refreshDeviceList();
    monitoring_ = true;
    try {
        monitor_thread_ = std::thread(&AudioDeviceMonitor::monitoringLoop, this);
    } catch (...) {
        monitoring_ = false;
        running_ = false;
        throw;
    }
}

void AudioDeviceMonitor::stop() {
    {
        std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
        monitoring_ = false;
        running_ = false;
        wait_cv_.notify_all();
    }
    if (monitor_thread_.joinable()) {
        if (std::this_thread::get_id() == monitor_thread_.get_id()) {
            return;
        }
        monitor_thread_.join();
    }
}

void AudioDeviceMonitor::setDeviceEventCallback(DeviceEventCallback callback) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    device_event_callback_ = std::move(callback);
}

std::vector<AudioDeviceInfo> AudioDeviceMonitor::getInputDevices() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    std::vector<AudioDeviceInfo> result;
    for (const auto& device : input_devices_) {
        if (device.is_connected) {
            result.push_back(device);
        }
    }
    return result;
}

std::vector<AudioDeviceInfo> AudioDeviceMonitor::getOutputDevices() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    std::vector<AudioDeviceInfo> result;
    for (const auto& device : output_devices_) {
        if (device.is_connected) {
            result.push_back(device);
        }
    }
    return result;
}

AudioDeviceInfo AudioDeviceMonitor::getDefaultInputDevice() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    for (const auto& device : input_devices_) {
        if (device.is_default && device.is_connected) {
            return device;
        }
    }
    for (const auto& device : input_devices_) {
        if (device.is_connected) {
            return device;
        }
    }
    return AudioDeviceInfo();
}

AudioDeviceInfo AudioDeviceMonitor::getDefaultOutputDevice() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    for (const auto& device : output_devices_) {
        if (device.is_default && device.is_connected) {
            return device;
        }
    }
    for (const auto& device : output_devices_) {
        if (device.is_connected) {
            return device;
        }
    }
    return AudioDeviceInfo();
}

void AudioDeviceMonitor::refreshDeviceList() {
    detectDeviceChanges();
}

bool AudioDeviceMonitor::isDeviceConnected(const std::string& device_id) const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    const auto connected = [&device_id](const AudioDeviceInfo& device) {
        return device.device_id == device_id && device.is_connected;
    };
    return std::any_of(input_devices_.begin(), input_devices_.end(), connected) ||
           std::any_of(output_devices_.begin(), output_devices_.end(), connected);
}

void AudioDeviceMonitor::monitoringLoop() {
    std::unique_lock<std::mutex> wait_lock(wait_mutex_);
    while (monitoring_) {
        detectDeviceChanges();
        wait_cv_.wait_for(wait_lock, std::chrono::milliseconds(DEVICE_POLL_INTERVAL_MS),
                          [this] { return !monitoring_.load(); });
    }
}

void AudioDeviceMonitor::detectDeviceChanges() {
    if (!portaudio_initialized_) {
        const PaError error = Pa_Initialize();
        if (error != paNoError) {
            return;
        }
        portaudio_initialized_ = true;
    }

    std::vector<AudioDeviceInfo> new_inputs;
    std::vector<AudioDeviceInfo> new_outputs;
    const int count = Pa_GetDeviceCount();
    const int default_input = Pa_GetDefaultInputDevice();
    const int default_output = Pa_GetDefaultOutputDevice();

    if (count >= 0) {
        for (int index = 0; index < count; ++index) {
            const PaDeviceInfo* info = Pa_GetDeviceInfo(index);
            if (!info) {
                continue;
            }
            if (info->maxInputChannels > 0) {
                AudioDeviceInfo device;
                device.device_id = std::to_string(index);
                device.device_name = info->name;
                device.is_default = index == default_input;
                device.is_input = true;
                new_inputs.push_back(device);
            }
            if (info->maxOutputChannels > 0) {
                AudioDeviceInfo device;
                device.device_id = std::to_string(index);
                device.device_name = info->name;
                device.is_default = index == default_output;
                device.is_output = true;
                new_outputs.push_back(device);
            }
        }
    }

    std::vector<DeviceEvent> events;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        for (const auto& previous : input_devices_) {
            const bool still_present = std::any_of(
                new_inputs.begin(), new_inputs.end(),
                [&previous](const AudioDeviceInfo& device) {
                    return device.device_id == previous.device_id;
                });
            if (!still_present && previous.is_connected) {
                events.push_back({DeviceEventType::Disconnected,
                                  previous.device_name, previous.device_id});
            }
        }
        for (const auto& device : new_inputs) {
            const bool existed = std::any_of(
                input_devices_.begin(), input_devices_.end(),
                [&device](const AudioDeviceInfo& previous) {
                    return previous.device_id == device.device_id;
                });
            if (!existed) {
                events.push_back({DeviceEventType::Connected,
                                  device.device_name, device.device_id});
            }
        }

        for (const auto& previous : output_devices_) {
            const bool still_present = std::any_of(
                new_outputs.begin(), new_outputs.end(),
                [&previous](const AudioDeviceInfo& device) {
                    return device.device_id == previous.device_id;
                });
            if (!still_present && previous.is_connected) {
                events.push_back({DeviceEventType::Disconnected,
                                  previous.device_name, previous.device_id});
            }
        }
        for (const auto& device : new_outputs) {
            const bool existed = std::any_of(
                output_devices_.begin(), output_devices_.end(),
                [&device](const AudioDeviceInfo& previous) {
                    return previous.device_id == device.device_id;
                });
            if (!existed) {
                events.push_back({DeviceEventType::Connected,
                                  device.device_name, device.device_id});
            }
        }

        input_devices_ = std::move(new_inputs);
        output_devices_ = std::move(new_outputs);
    }

    for (const auto& event : events) {
        notifyDeviceEvent(event);
    }
}

void AudioDeviceMonitor::notifyDeviceEvent(const DeviceEvent& event) {
    DeviceEventCallback callback;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        callback = device_event_callback_;
    }
    if (callback) {
        callback(event);
    }
}

void AudioDeviceMonitor::initializeDevices() {
    if (portaudio_initialized_) {
        return;
    }
    const PaError error = Pa_Initialize();
    if (error != paNoError) {
        return;
    }
    portaudio_initialized_ = true;
    detectDeviceChanges();
}
