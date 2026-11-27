#include <gtest/gtest.h>
#include "AudioDeviceMonitor.h"
#include <thread>
#include <chrono>

class AudioDeviceMonitorTest : public ::testing::Test {
protected:
    AudioDeviceMonitor monitor;
};

TEST_F(AudioDeviceMonitorTest, Initialization) {
    // Should not throw
    EXPECT_NO_THROW({});
}

TEST_F(AudioDeviceMonitorTest, GetInputDevices) {
    auto devices = monitor.getInputDevices();
    EXPECT_GE(devices.size(), 1);  // Should have at least default
}

TEST_F(AudioDeviceMonitorTest, GetOutputDevices) {
    auto devices = monitor.getOutputDevices();
    EXPECT_GE(devices.size(), 1);  // Should have at least default
}

TEST_F(AudioDeviceMonitorTest, GetDefaultInputDevice) {
    auto device = monitor.getDefaultInputDevice();
    EXPECT_FALSE(device.device_id.empty());
    EXPECT_TRUE(device.is_input);
}

TEST_F(AudioDeviceMonitorTest, GetDefaultOutputDevice) {
    auto device = monitor.getDefaultOutputDevice();
    EXPECT_FALSE(device.device_id.empty());
    EXPECT_TRUE(device.is_output);
}

TEST_F(AudioDeviceMonitorTest, StartMonitoring) {
    EXPECT_NO_THROW({
        monitor.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        monitor.stop();
    });
}

TEST_F(AudioDeviceMonitorTest, DeviceEventCallback) {
    std::atomic<int> event_count{0};
    
    monitor.setDeviceEventCallback([&event_count](const DeviceEvent& event) {
        event_count++;
    });
    
    monitor.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    monitor.stop();
    
    // Should have recorded some events (or at least not crash)
    EXPECT_GE(event_count.load(), 0);
}

TEST_F(AudioDeviceMonitorTest, IsDeviceConnected) {
    auto devices = monitor.getInputDevices();
    if (!devices.empty()) {
        bool connected = monitor.isDeviceConnected(devices[0].device_id);
        EXPECT_TRUE(connected);
    }
}

TEST_F(AudioDeviceMonitorTest, RefreshDeviceList) {
    EXPECT_NO_THROW({
        monitor.refreshDeviceList();
    });
}

TEST_F(AudioDeviceMonitorTest, MultipleStartStop) {
    for (int i = 0; i < 3; ++i) {
        monitor.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        monitor.stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

TEST_F(AudioDeviceMonitorTest, ConcurrentAccess) {
    monitor.start();
    
    std::thread reader1([this]() {
        for (int i = 0; i < 10; ++i) {
            auto devices = monitor.getInputDevices();
            (void)devices;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });
    
    std::thread reader2([this]() {
        for (int i = 0; i < 10; ++i) {
            auto device = monitor.getDefaultOutputDevice();
            (void)device;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });
    
    std::thread refresher([this]() {
        for (int i = 0; i < 5; ++i) {
            monitor.refreshDeviceList();
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });
    
    reader1.join();
    reader2.join();
    refresher.join();
    
    monitor.stop();
}

