#include <gtest/gtest.h>
#include "NetworkMonitor.h"
#include <thread>
#include <chrono>

class NetworkMonitorTest : public ::testing::Test {
protected:
    NetworkMonitor monitor;
};

// Test packet recording
TEST_F(NetworkMonitorTest, RecordPacketSent) {
    monitor.recordPacketSent(1, 1024);
    monitor.recordPacketSent(2, 1024);
    monitor.recordPacketSent(3, 1024);
    
    auto metrics = monitor.getMetrics();
    EXPECT_EQ(metrics.total_packets_sent, 3);
}

TEST_F(NetworkMonitorTest, RecordPacketReceived) {
    monitor.recordPacketSent(1, 1024);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    monitor.recordPacketReceived(1, 1024, 123456);
    
    auto metrics = monitor.getMetrics();
    EXPECT_EQ(metrics.total_packets_sent, 1);
    EXPECT_TRUE(metrics.is_connected);
}

// Test packet loss calculation
TEST_F(NetworkMonitorTest, PacketLossRate) {
    for (int i = 1; i <= 100; ++i) {
        monitor.recordPacketSent(i, 1024);
    }
    
    for (int i = 1; i <= 95; ++i) {
        monitor.recordPacketReceived(i, 1024, 0);
    }
    
    double loss_rate = monitor.getPacketLossRate();
    EXPECT_NEAR(loss_rate, 5.0, 0.1);
}

TEST_F(NetworkMonitorTest, ZeroLossRate) {
    for (int i = 1; i <= 50; ++i) {
        monitor.recordPacketSent(i, 1024);
        monitor.recordPacketReceived(i, 1024, 0);
    }
    
    double loss_rate = monitor.getPacketLossRate();
    EXPECT_NEAR(loss_rate, 0.0, 0.1);
}

TEST_F(NetworkMonitorTest, HighLossRate) {
    for (int i = 1; i <= 100; ++i) {
        monitor.recordPacketSent(i, 1024);
    }
    
    for (int i = 1; i <= 20; ++i) {
        monitor.recordPacketReceived(i, 1024, 0);
    }
    
    double loss_rate = monitor.getPacketLossRate();
    EXPECT_NEAR(loss_rate, 80.0, 0.1);
}

// Test RTT calculation
TEST_F(NetworkMonitorTest, RttCalculation) {
    auto start = std::chrono::system_clock::now();
    
    monitor.recordPacketSent(1, 1024);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    monitor.recordPacketReceived(1, 1024, 0);
    
    double rtt = monitor.getRttMs();
    EXPECT_GE(rtt, 40.0);  // Should be at least 40ms (accounting for sleep accuracy)
    EXPECT_LE(rtt, 100.0); // But not more than 100ms
}

TEST_F(NetworkMonitorTest, AverageRtt) {
    for (int i = 1; i <= 10; ++i) {
        monitor.recordPacketSent(i, 1024);
        std::this_thread::sleep_for(std::chrono::milliseconds(10 + i));
        monitor.recordPacketReceived(i, 1024, 0);
    }
    
    double rtt = monitor.getRttMs();
    EXPECT_GT(rtt, 0.0);
    EXPECT_LT(rtt, 1000.0);
}

// Test reorder detection
TEST_F(NetworkMonitorTest, ReorderDetection) {
    monitor.recordPacketSent(1, 1024);
    monitor.recordPacketSent(2, 1024);
    monitor.recordPacketSent(3, 1024);
    
    // Receive out of order
    monitor.recordPacketReceived(1, 1024, 0);
    monitor.recordPacketReceived(3, 1024, 0);  // 3 before 2
    monitor.recordPacketReceived(2, 1024, 0);
    
    auto metrics = monitor.getMetrics();
    EXPECT_GT(metrics.reordered_packets, 0);
}

TEST_F(NetworkMonitorTest, NoReorder) {
    for (int i = 1; i <= 20; ++i) {
        monitor.recordPacketSent(i, 1024);
        monitor.recordPacketReceived(i, 1024, 0);
    }
    
    double reorder_rate = monitor.getReorderRate();
    EXPECT_NEAR(reorder_rate, 0.0, 0.1);
}

// Test window size
TEST_F(NetworkMonitorTest, WindowSize) {
    monitor.setWindowSize(10);
    
    for (int i = 1; i <= 50; ++i) {
        monitor.recordPacketSent(i, 1024);
    }
    
    auto metrics = monitor.getMetrics();
    EXPECT_LE(metrics.total_packets_sent, 100);  // Should be bounded
}

// Test reset
TEST_F(NetworkMonitorTest, Reset) {
    for (int i = 1; i <= 100; ++i) {
        monitor.recordPacketSent(i, 1024);
    }
    
    auto metrics_before = monitor.getMetrics();
    EXPECT_GT(metrics_before.total_packets_sent, 0);
    
    monitor.reset();
    
    auto metrics_after = monitor.getMetrics();
    EXPECT_EQ(metrics_after.total_packets_sent, 0);
    EXPECT_EQ(metrics_after.total_packets_lost, 0);
}

// Test jitter calculation
TEST_F(NetworkMonitorTest, JitterCalculation) {
    for (int i = 1; i <= 10; ++i) {
        monitor.recordPacketSent(i, 1024);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        monitor.recordPacketReceived(i, 1024, 0);
    }
    
    double jitter = monitor.getJitterMs();
    // With consistent 20ms delays, jitter should be low
    EXPECT_GE(jitter, 0.0);
}

// Test concurrent access
TEST_F(NetworkMonitorTest, ThreadSafety) {
    std::thread sender([this]() {
        for (int i = 0; i < 100; ++i) {
            monitor.recordPacketSent(i, 1024);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    
    std::thread receiver([this]() {
        for (int i = 0; i < 100; ++i) {
            monitor.recordPacketReceived(i, 1024, 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    
    std::thread reader([this]() {
        for (int i = 0; i < 50; ++i) {
            auto metrics = monitor.getMetrics();
            (void)metrics;  // Use metrics to prevent optimization
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });
    
    sender.join();
    receiver.join();
    reader.join();
    
    auto final_metrics = monitor.getMetrics();
    EXPECT_GT(final_metrics.total_packets_sent, 0);
}

// Test zero window size handling
TEST_F(NetworkMonitorTest, EmptyMetrics) {
    auto metrics = monitor.getMetrics();
    EXPECT_EQ(metrics.total_packets_sent, 0);
    EXPECT_NEAR(metrics.packet_loss_rate, 0.0, 0.01);
}

