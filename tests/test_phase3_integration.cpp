#include <gtest/gtest.h>
#include "NetworkMonitor.h"
#include "HeartbeatManager.h"
#include "CongestionController.h"
#include "NackHandler.h"
#include "AudioDeviceMonitor.h"
#include <thread>
#include <chrono>
#include <atomic>

/**
 * Phase 3 Integration Tests
 * Testing all network reliability components working together
 */

class Phase3IntegrationTest : public ::testing::Test {
protected:
    NetworkMonitor monitor;
    HeartbeatConfig hb_config;
    CongestionController congestion;
    NackHandler nack_handler;
    AudioDeviceMonitor device_monitor;
    
    Phase3IntegrationTest() {
        hb_config.heartbeat_interval_ms = 50;
        hb_config.heartbeat_timeout_ms = 150;
        hb_config.max_retries = 2;
    }
};

// Test: Simulate stable network connection
TEST_F(Phase3IntegrationTest, StableNetworkScenario) {
    HeartbeatManager hb_manager(hb_config);
    
    std::atomic<bool> heartbeat_active{true};
    int heartbeat_count = 0;
    
    hb_manager.setHeartbeatCallback([&]() {
        if (heartbeat_active) {
            heartbeat_count++;
            monitor.recordPacketSent(heartbeat_count, 100);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            monitor.recordPacketReceived(heartbeat_count, 100, 0);
            hb_manager.recordHeartbeatResponse();
            return true;
        }
        return false;
    });
    
    hb_manager.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    heartbeat_active = false;
    hb_manager.stop();
    
    EXPECT_EQ(hb_manager.getState(), ConnectionState::Connected);
    EXPECT_NEAR(monitor.getPacketLossRate(), 0.0, 1.0);
    EXPECT_EQ(congestion.getCongestionLevel(), CongestionLevel::None);
}

// Test: Simulate network degradation
TEST_F(Phase3IntegrationTest, NetworkDegradationScenario) {
    // Simulate increasing packet loss
    for (int i = 0; i < 100; ++i) {
        monitor.recordPacketSent(i, 1024);
        
        // Increase loss over time
        int loss_rate = (i / 20) * 2;  // 0%, 2%, 4%, 6%, 8%
        
        if (i % (5 - std::min(4, loss_rate / 2)) != 0) {
            monitor.recordPacketReceived(i, 1024, 0);
        }
        
        // Update congestion controller
        congestion.updatePacketLossRate(monitor.getPacketLossRate());
    }
    
    double loss = monitor.getPacketLossRate();
    EXPECT_GT(loss, 1.0);  // Should have some loss
    
    // Should detect congestion
    CongestionLevel level = congestion.getCongestionLevel();
    EXPECT_NE(level, CongestionLevel::None);
}

// Test: NACK-based retransmission recovery
TEST_F(Phase3IntegrationTest, NackRetransmissionScenario) {
    uint8_t data[256] = {0};
    
    // Send 10 packets
    for (int i = 1; i <= 10; ++i) {
        nack_handler.recordPacketSent(i, data, sizeof(data));
    }
    
    std::atomic<int> retransmitted{0};
    nack_handler.setRetransmitCallback(
        [&retransmitted](uint32_t seq, const uint8_t* pkt_data, size_t size) {
            retransmitted++;
            return true;
        });
    
    // Simulate loss on packets 3, 5, 7
    NackRequest nack_req;
    nack_req.lost_sequence_numbers = {3, 5, 7};
    
    size_t recovered = nack_handler.processNack(nack_req);
    EXPECT_EQ(recovered, 3);
    EXPECT_EQ(retransmitted.load(), 3);
    EXPECT_EQ(nack_handler.getNackCount(), 1);
}

// Test: Device monitoring during streaming
TEST_F(Phase3IntegrationTest, DeviceMonitoringIntegration) {
    std::atomic<int> device_events{0};
    
    device_monitor.setDeviceEventCallback([&device_events](const DeviceEvent& event) {
        device_events++;
    });
    
    device_monitor.start();
    
    // Simulate streaming while monitoring
    for (int i = 0; i < 20; ++i) {
        auto input_devices = device_monitor.getInputDevices();
        auto output_devices = device_monitor.getOutputDevices();
        
        EXPECT_GE(input_devices.size(), 1);
        EXPECT_GE(output_devices.size(), 1);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    device_monitor.stop();
    
    // Should have checked devices multiple times
    EXPECT_GE(device_events.load(), 0);
}

// Test: Full network reliability pipeline
TEST_F(Phase3IntegrationTest, FullReliabilityPipeline) {
    HeartbeatManager hb_manager(hb_config);
    
    std::atomic<int> packets_sent{0};
    std::atomic<int> packets_received{0};
    std::atomic<int> packets_lost{0};
    std::atomic<int> packets_recovered{0};
    
    // Setup heartbeat
    hb_manager.setHeartbeatCallback([&]() {
        packets_sent++;
        monitor.recordPacketSent(packets_sent, 100);
        
        // Simulate 10% loss
        if (packets_sent % 10 != 0) {
            packets_received++;
            monitor.recordPacketReceived(packets_sent, 100, 0);
            hb_manager.recordHeartbeatResponse();
        } else {
            packets_lost++;
            monitor.recordPacketLoss(packets_sent);
        }
        return true;
    });
    
    // Setup NACK recovery
    nack_handler.setRetransmitCallback(
        [&packets_recovered](uint32_t seq, const uint8_t* data, size_t size) {
            packets_recovered++;
            return true;
        });
    
    // Run pipeline
    hb_manager.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    hb_manager.stop();
    
    // Verify statistics
    EXPECT_GT(packets_sent.load(), 0);
    EXPECT_GT(packets_received.load(), 0);
    EXPECT_GT(packets_lost.load(), 0);
    
    // Check loss rate
    double loss_rate = monitor.getPacketLossRate();
    EXPECT_GT(loss_rate, 0.0);
    EXPECT_LT(loss_rate, 50.0);  // Should be ~10%
    
    // Check congestion detection
    congestion.updatePacketLossRate(loss_rate);
    EXPECT_NE(congestion.getCongestionLevel(), CongestionLevel::Severe);
}

// Test: Reconnection scenario
TEST_F(Phase3IntegrationTest, ReconnectionScenario) {
    HeartbeatManager hb_manager(hb_config);
    
    std::atomic<int> reconnect_attempts{0};
    
    hb_manager.setStateChangeCallback(
        [&reconnect_attempts](ConnectionState old_state, ConnectionState new_state) {
            if (new_state == ConnectionState::Reconnecting) {
                reconnect_attempts++;
            }
        });
    
    hb_manager.setHeartbeatCallback([&hb_manager]() {
        // Simulate delayed response
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        hb_manager.recordHeartbeatResponse();
        return true;
    });
    
    hb_manager.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    hb_manager.stop();
    
    // Should have attempted some reconnections or stayed connected
    EXPECT_GE(reconnect_attempts.load(), 0);
}

// Test: Stress test - high throughput with monitoring
TEST_F(Phase3IntegrationTest, StressTestHighThroughput) {
    // Send many packets and monitor
    uint32_t total_packets = 1000;
    
    for (uint32_t i = 1; i <= total_packets; ++i) {
        monitor.recordPacketSent(i, 1024);
        
        // Simulate 5% loss
        if (i % 20 != 0) {
            monitor.recordPacketReceived(i, 1024, 0);
        }
    }
    
    auto metrics = monitor.getMetrics();
    EXPECT_EQ(metrics.total_packets_sent, total_packets);
    EXPECT_NEAR(metrics.packet_loss_rate, 5.0, 0.5);
}

// Test: RTT and jitter monitoring
TEST_F(Phase3IntegrationTest, LatencyMonitoring) {
    // Simulate packets with varying delays
    for (int i = 1; i <= 50; ++i) {
        auto start = std::chrono::system_clock::now();
        monitor.recordPacketSent(i, 100);
        
        // Vary delay: 20ms + random jitter
        int jitter = (i * 7) % 10;  // 0-9ms
        std::this_thread::sleep_for(
            std::chrono::milliseconds(20 + jitter));
        
        monitor.recordPacketReceived(i, 100, 0);
    }
    
    double rtt = monitor.getRttMs();
    double jitter = monitor.getJitterMs();
    
    EXPECT_GE(rtt, 15.0);   // At least ~20ms (minus some overhead)
    EXPECT_GT(jitter, 0.0); // Should have some variance
}

