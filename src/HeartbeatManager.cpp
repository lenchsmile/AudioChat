#include "HeartbeatManager.h"
#include <thread>
#include <cmath>

HeartbeatManager::HeartbeatManager(const HeartbeatConfig& config)
    : config_(config), state_(ConnectionState::Disconnected) {
    last_response_time_us_ = 
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
}

HeartbeatManager::~HeartbeatManager() {
    stop();
}

void HeartbeatManager::start() {
    if (running_.exchange(true)) {
        return;  // Already running
    }
    
    changeState(ConnectionState::Connected);
    heartbeat_thread_ = std::thread(&HeartbeatManager::heartbeatLoop, this);
}

void HeartbeatManager::stop() {
    running_ = false;
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
}

void HeartbeatManager::recordHeartbeatResponse() {
    last_response_time_us_ = 
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    
    reconnect_attempts_ = 0;
    
    if (state_ != ConnectionState::Connected) {
        changeState(ConnectionState::Connected);
    }
}

ConnectionState HeartbeatManager::getState() const {
    return state_.load(std::memory_order_acquire);
}

bool HeartbeatManager::isConnected() const {
    return getState() == ConnectionState::Connected;
}

void HeartbeatManager::setHeartbeatCallback(SendHeartbeatCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    send_callback_ = callback;
}

void HeartbeatManager::setStateChangeCallback(StateChangeCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    state_callback_ = callback;
}

int64_t HeartbeatManager::getTimeSinceLastResponse() const {
    auto now = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    return (now - last_response_time_us_) / 1000;  // Convert to ms
}

uint32_t HeartbeatManager::getReconnectionAttempts() const {
    return reconnect_attempts_.load();
}

void HeartbeatManager::reset() {
    recordHeartbeatResponse();
    changeState(ConnectionState::Connected);
}

void HeartbeatManager::forceReconnect() {
    changeState(ConnectionState::Reconnecting);
    reconnect_attempts_ = 0;
}

void HeartbeatManager::heartbeatLoop() {
    while (running_) {
        int64_t time_since_response = getTimeSinceLastResponse();
        
        // Check if timed out
        if (time_since_response > config_.heartbeat_timeout_ms && 
            state_ == ConnectionState::Connected) {
            
            // Try to reconnect
            if (reconnect_attempts_ < config_.max_retries) {
                changeState(ConnectionState::Reconnecting);
            } else {
                changeState(ConnectionState::Failed);
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(config_.heartbeat_interval_ms));
                continue;
            }
        }
        
        // Handle reconnection backoff
        if (state_ == ConnectionState::Reconnecting) {
            int64_t backoff_ms = calculateBackoffMs();
            std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
            
            if (attemptReconnect()) {
                recordHeartbeatResponse();
            } else {
                reconnect_attempts_++;
            }
            continue;
        }
        
        // Send heartbeat if connected
        if (state_ == ConnectionState::Connected) {
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                if (send_callback_) {
                    send_callback_();
                }
            }
        }
        
        std::this_thread::sleep_for(
            std::chrono::milliseconds(config_.heartbeat_interval_ms));
    }
}

void HeartbeatManager::changeState(ConnectionState new_state) {
    ConnectionState old_state = state_.exchange(new_state, std::memory_order_acq_rel);
    
    if (old_state != new_state) {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        if (state_callback_) {
            state_callback_(old_state, new_state);
        }
    }
}

int64_t HeartbeatManager::calculateBackoffMs() const {
    uint32_t attempts = reconnect_attempts_.load();
    
    int64_t backoff = config_.initial_backoff_ms * 
        static_cast<int64_t>(std::pow(config_.backoff_multiplier, attempts));
    
    return std::min(backoff, static_cast<int64_t>(config_.max_backoff_ms));
}

bool HeartbeatManager::attemptReconnect() {
    // Try to send a heartbeat to reconnect
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (send_callback_) {
        return send_callback_();
    }
    return false;
}

