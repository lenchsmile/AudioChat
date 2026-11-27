#include "AudioDeviceMonitor.h"
#include <algorithm>
#include <chrono>

AudioDeviceMonitor::AudioDeviceMonitor() = default;

AudioDeviceMonitor::~AudioDeviceMonitor() {
    stop();
}

void AudioDeviceMonitor::start() {
    if (running_) {
        return;
    }
    
    running_ = true;
    monitoring_ = true;
    
    initializeDevices();
    
    monitor_thread_ = std::thread(&AudioDeviceMonitor::monitoringLoop, this);
}

void AudioDeviceMonitor::stop() {
    running_ = false;
    monitoring_ = false;
    
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
}

void AudioDeviceMonitor::setDeviceEventCallback(DeviceEventCallback callback) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    device_event_callback_ = callback;
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
    
    // Return first available if no default
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
    
    // Return first available if no default
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
    
    auto check = [&device_id](const AudioDeviceInfo& dev) {
        return dev.device_id == device_id && dev.is_connected;
    };
    
    auto in_it = std::find_if(input_devices_.begin(), input_devices_.end(), check);
    if (in_it != input_devices_.end()) {
        return true;
    }
    
    auto out_it = std::find_if(output_devices_.begin(), output_devices_.end(), check);
    return out_it != output_devices_.end();
}

void AudioDeviceMonitor::monitoringLoop() {
    std::vector<AudioDeviceInfo> prev_input_devices;
    std::vector<AudioDeviceInfo> prev_output_devices;
    
    while (monitoring_) {
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            prev_input_devices = input_devices_;
            prev_output_devices = output_devices_;
        }
        
        detectDeviceChanges();
        
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            
            // Check for device changes
            for (const auto& prev_dev : prev_input_devices) {
                auto it = std::find_if(input_devices_.begin(), input_devices_.end(),
                    [&prev_dev](const AudioDeviceInfo& dev) {
                        return dev.device_id == prev_dev.device_id;
                    });
                
                if (it == input_devices_.end() && prev_dev.is_connected) {
                    // Device disconnected
                    DeviceEvent event;
                    event.type = DeviceEventType::Disconnected;
                    event.device_id = prev_dev.device_id;
                    event.device_name = prev_dev.device_name;
                    notifyDeviceEvent(event);
                }
            }
            
            for (const auto& device : input_devices_) {
                auto it = std::find_if(prev_input_devices.begin(), prev_input_devices.end(),
                    [&device](const AudioDeviceInfo& dev) {
                        return dev.device_id == device.device_id;
                    });
                
                if (it == prev_input_devices.end() && device.is_connected) {
                    // Device connected
                    DeviceEvent event;
                    event.type = DeviceEventType::Connected;
                    event.device_id = device.device_id;
                    event.device_name = device.device_name;
                    notifyDeviceEvent(event);
                }
            }
        }
        
        std::this_thread::sleep_for(
            std::chrono::milliseconds(DEVICE_POLL_INTERVAL_MS));
    }
}

void AudioDeviceMonitor::detectDeviceChanges() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    // This is a simplified implementation
    // In a real implementation, would use Windows API or PortAudio
    // to get actual device list changes
    
    // Placeholder: devices are marked as connected by default
    for (auto& device : input_devices_) {
        device.is_connected = true;
    }
    for (auto& device : output_devices_) {
        device.is_connected = true;
    }
}

void AudioDeviceMonitor::notifyDeviceEvent(const DeviceEvent& event) {
    if (device_event_callback_) {
        device_event_callback_(event);
    }
}

void AudioDeviceMonitor::initializeDevices() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    // Initialize with default devices
    AudioDeviceInfo default_input;
    default_input.device_id = "default_input";
    default_input.device_name = "Default Audio Input";
    default_input.is_default = true;
    default_input.is_input = true;
    default_input.is_connected = true;
    input_devices_.push_back(default_input);
    
    AudioDeviceInfo default_output;
    default_output.device_id = "default_output";
    default_output.device_name = "Default Audio Output";
    default_output.is_default = true;
    default_output.is_output = true;
    default_output.is_connected = true;
    output_devices_.push_back(default_output);
}

