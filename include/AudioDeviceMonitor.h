#pragma once

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>

/**
 * @enum DeviceEventType
 * @brief Audio device event types
 */
enum class DeviceEventType {
    Connected,          // Device connected
    Disconnected,       // Device disconnected
    DefaultChanged,     // Default device changed
    StateChanged        // Device state changed
};

/**
 * @struct DeviceEvent
 * @brief Audio device event information
 */
struct DeviceEvent {
    DeviceEventType type;
    std::string device_name;
    std::string device_id;
};

/**
 * @struct AudioDeviceInfo
 * @brief Audio device information
 */
struct AudioDeviceInfo {
    std::string device_id;
    std::string device_name;
    bool is_default = false;
    bool is_input = false;
    bool is_output = false;
    bool is_connected = true;
};

/**
 * @class AudioDeviceMonitor
 * @brief Monitor audio device connection/disconnection
 * 
 * Features:
 * - Monitor device connect/disconnect events
 * - Track default device changes
 * - Automatic failover support
 * - Device list refresh
 * - Event callbacks
 * 
 * Note: Windows-specific implementation using WM_DEVICECHANGE
 */
class AudioDeviceMonitor {
public:
    using DeviceEventCallback = std::function<void(const DeviceEvent& event)>;
    
    AudioDeviceMonitor();
    ~AudioDeviceMonitor();
    
    /**
     * @brief Start monitoring devices
     */
    void start();
    
    /**
     * @brief Stop monitoring
     */
    void stop();
    
    /**
     * @brief Set callback for device events
     * @param callback Function to call on device event
     */
    void setDeviceEventCallback(DeviceEventCallback callback);
    
    /**
     * @brief Get list of available input devices
     * @return Vector of AudioDeviceInfo
     */
    std::vector<AudioDeviceInfo> getInputDevices() const;
    
    /**
     * @brief Get list of available output devices
     * @return Vector of AudioDeviceInfo
     */
    std::vector<AudioDeviceInfo> getOutputDevices() const;
    
    /**
     * @brief Get default input device
     * @return AudioDeviceInfo for default input
     */
    AudioDeviceInfo getDefaultInputDevice() const;
    
    /**
     * @brief Get default output device
     * @return AudioDeviceInfo for default output
     */
    AudioDeviceInfo getDefaultOutputDevice() const;
    
    /**
     * @brief Refresh device list
     */
    void refreshDeviceList();
    
    /**
     * @brief Check if device is connected
     * @param device_id Device identifier
     * @return True if connected
     */
    bool isDeviceConnected(const std::string& device_id) const;

private:
    std::atomic<bool> running_{false};
    std::atomic<bool> monitoring_{false};
    mutable std::mutex state_mutex_;
    std::mutex lifecycle_mutex_;
    std::mutex wait_mutex_;
    std::condition_variable wait_cv_;
    bool portaudio_initialized_ = false;

    std::vector<AudioDeviceInfo> input_devices_;
    std::vector<AudioDeviceInfo> output_devices_;

    DeviceEventCallback device_event_callback_;

    static constexpr int DEVICE_POLL_INTERVAL_MS = 1000;
    std::thread monitor_thread_;

    void monitoringLoop();
    void detectDeviceChanges();
    void notifyDeviceEvent(const DeviceEvent& event);
    void initializeDevices();
};

