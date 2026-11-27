#pragma once

#include <cstdint>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <functional>
#include <memory>

/**
 * @enum ConnectionState
 * @brief Connection state machine states
 */
enum class ConnectionState {
    Connected,          // Active connection
    Disconnected,       // No connection
    Reconnecting,       // Attempting to reconnect
    Failed              // Reconnection failed
};

/**
 * @struct HeartbeatConfig
 * @brief Configuration for heartbeat mechanism
 */
struct HeartbeatConfig {
    // Heartbeat timing
    int heartbeat_interval_ms = 1000;      // Send heartbeat every 1s
    int heartbeat_timeout_ms = 5000;       // No response after 5s = disconnected
    int max_retries = 5;                   // Maximum reconnection attempts
    
    // Backoff strategy
    int initial_backoff_ms = 100;
    int max_backoff_ms = 30000;
    double backoff_multiplier = 2.0;
};

/**
 * @class HeartbeatManager
 * @brief Manage connection heartbeat and reconnection
 * 
 * Features:
 * - Periodic heartbeat packets
 * - Timeout-based disconnection detection
 * - Automatic reconnection with exponential backoff
 * - Connection state machine
 * - Event callbacks
 */
class HeartbeatManager {
public:
    using StateChangeCallback = std::function<void(ConnectionState old_state, 
                                                    ConnectionState new_state)>;
    using SendHeartbeatCallback = std::function<bool()>;
    
    explicit HeartbeatManager(const HeartbeatConfig& config = HeartbeatConfig());
    ~HeartbeatManager();
    
    /**
     * @brief Start heartbeat manager
     */
    void start();
    
    /**
     * @brief Stop heartbeat manager
     */
    void stop();
    
    /**
     * @brief Record heartbeat response received
     */
    void recordHeartbeatResponse();
    
    /**
     * @brief Get current connection state
     * @return Current ConnectionState
     */
    ConnectionState getState() const;
    
    /**
     * @brief Check if currently connected
     * @return True if connected
     */
    bool isConnected() const;
    
    /**
     * @brief Set callback for heartbeat send
     * @param callback Function to call when sending heartbeat
     */
    void setHeartbeatCallback(SendHeartbeatCallback callback);
    
    /**
     * @brief Set callback for state changes
     * @param callback Function to call on state change
     */
    void setStateChangeCallback(StateChangeCallback callback);
    
    /**
     * @brief Get time since last response
     * @return Milliseconds
     */
    int64_t getTimeSinceLastResponse() const;
    
    /**
     * @brief Get current reconnection attempt count
     * @return Attempt number
     */
    uint32_t getReconnectionAttempts() const;
    
    /**
     * @brief Reset to connected state
     */
    void reset();
    
    /**
     * @brief Force reconnection
     */
    void forceReconnect();

private:
    enum class HeartbeatState {
        Idle,
        Sending,
        Waiting,
        Timeout,
        Backoff
    };
    
    HeartbeatConfig config_;
    std::atomic<ConnectionState> state_{ConnectionState::Disconnected};
    std::atomic<HeartbeatState> hb_state_{HeartbeatState::Idle};
    
    // Timing
    std::atomic<int64_t> last_response_time_us_{0};
    std::atomic<uint32_t> reconnect_attempts_{0};
    
    // Thread management
    std::thread heartbeat_thread_;
    std::atomic<bool> running_{false};
    std::mutex lifecycle_mutex_;
    std::mutex wait_mutex_;
    std::condition_variable wait_cv_;
    
    // Callbacks
    std::mutex callback_mutex_;
    SendHeartbeatCallback send_callback_;
    StateChangeCallback state_callback_;
    
    // Helper methods
    void heartbeatLoop();
    void changeState(ConnectionState new_state);
    int64_t calculateBackoffMs() const;
    bool attemptReconnect();
};

