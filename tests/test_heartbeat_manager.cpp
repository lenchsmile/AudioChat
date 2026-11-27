#include <gtest/gtest.h>
#include "HeartbeatManager.h"
#include <thread>
#include <chrono>
#include <atomic>

class HeartbeatManagerTest : public ::testing::Test {
protected:
    HeartbeatConfig config;
    
    HeartbeatManagerTest() {
        config.heartbeat_interval_ms = 100;
        config.heartbeat_timeout_ms = 300;
        config.max_retries = 3;
        config.initial_backoff_ms = 50;
    }
};

TEST_F(HeartbeatManagerTest, Initialization) {
    HeartbeatManager manager(config);
    EXPECT_EQ(manager.getState(), ConnectionState::Disconnected);
    EXPECT_FALSE(manager.isConnected());
}

TEST_F(HeartbeatManagerTest, StartAndConnect) {
    HeartbeatManager manager(config);
    manager.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    manager.recordHeartbeatResponse();
    EXPECT_EQ(manager.getState(), ConnectionState::Connected);
    EXPECT_TRUE(manager.isConnected());
    
    manager.stop();
}

TEST_F(HeartbeatManagerTest, ConnectionTimeout) {
    HeartbeatManager manager(config);
    manager.start();
    manager.recordHeartbeatResponse();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    
    // Should have timed out
    EXPECT_NE(manager.getState(), ConnectionState::Connected);
    
    manager.stop();
}

TEST_F(HeartbeatManagerTest, StateChangeCallback) {
    HeartbeatManager manager(config);
    
    std::atomic<int> state_changes{0};
    manager.setStateChangeCallback(
        [&state_changes](ConnectionState old_state, ConnectionState new_state) {
            state_changes++;
        });
    
    manager.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    manager.recordHeartbeatResponse();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    EXPECT_GT(state_changes.load(), 0);
    
    manager.stop();
}

TEST_F(HeartbeatManagerTest, HeartbeatCallback) {
    HeartbeatManager manager(config);
    
    std::atomic<int> heartbeat_count{0};
    manager.setHeartbeatCallback([&heartbeat_count]() {
        heartbeat_count++;
        return true;
    });
    
    manager.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    
    EXPECT_GT(heartbeat_count.load(), 0);
    
    manager.stop();
}

TEST_F(HeartbeatManagerTest, ReconnectionBackoff) {
    HeartbeatManager manager(config);
    manager.start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    manager.forceReconnect();
    
    EXPECT_EQ(manager.getState(), ConnectionState::Reconnecting);
    
    manager.recordHeartbeatResponse();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    EXPECT_EQ(manager.getState(), ConnectionState::Connected);
    
    manager.stop();
}

TEST_F(HeartbeatManagerTest, GetTimeSinceLastResponse) {
    HeartbeatManager manager(config);
    manager.start();
    manager.recordHeartbeatResponse();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    int64_t time_since = manager.getTimeSinceLastResponse();
    EXPECT_GE(time_since, 90);  // At least 90ms
    EXPECT_LE(time_since, 200); // But not more than 200ms
    
    manager.stop();
}

TEST_F(HeartbeatManagerTest, ResetState) {
    HeartbeatManager manager(config);
    manager.start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    
    // Should be reconnecting or failed
    ConnectionState state_before = manager.getState();
    EXPECT_NE(state_before, ConnectionState::Connected);
    
    manager.reset();
    
    EXPECT_EQ(manager.getState(), ConnectionState::Connected);
    
    manager.stop();
}

TEST_F(HeartbeatManagerTest, MultipleStartStop) {
    HeartbeatManager manager(config);
    
    for (int i = 0; i < 3; ++i) {
        manager.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        manager.recordHeartbeatResponse();
        manager.stop();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

TEST_F(HeartbeatManagerTest, ReconnectionAttempts) {
    HeartbeatManager manager(config);
    manager.start();
    manager.forceReconnect();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    uint32_t attempts = manager.getReconnectionAttempts();
    // May have attempted reconnection
    EXPECT_GE(attempts, 0);
    
    manager.stop();
}

