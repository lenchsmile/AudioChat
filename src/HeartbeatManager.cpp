#include "HeartbeatManager.h"

#include <algorithm>
#include <cmath>
#include <thread>

HeartbeatManager::HeartbeatManager(const HeartbeatConfig& config)
    : config_(config), state_(ConnectionState::Disconnected) {
    config_.heartbeat_interval_ms = std::max(1, config_.heartbeat_interval_ms);
    config_.heartbeat_timeout_ms = std::max(1, config_.heartbeat_timeout_ms);
    config_.max_retries = std::max(0, config_.max_retries);
    config_.initial_backoff_ms = std::max(1, config_.initial_backoff_ms);
    config_.max_backoff_ms = std::max(config_.initial_backoff_ms, config_.max_backoff_ms);
    config_.backoff_multiplier = std::max(1.0, config_.backoff_multiplier);

    last_response_time_us_ =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
}

HeartbeatManager::~HeartbeatManager() {
    stop();
}

void HeartbeatManager::start() {
    std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);
    if (running_.exchange(true)) {
        return;
    }

    changeState(ConnectionState::Connected);
    try {
        heartbeat_thread_ = std::thread(&HeartbeatManager::heartbeatLoop, this);
    } catch (...) {
        running_ = false;
        changeState(ConnectionState::Disconnected);
        throw;
    }
}

void HeartbeatManager::stop() {
    std::unique_lock<std::mutex> lifecycle_lock(lifecycle_mutex_);
    running_ = false;
    wait_cv_.notify_all();

    if (heartbeat_thread_.joinable()) {
        if (std::this_thread::get_id() == heartbeat_thread_.get_id()) {
            return;
        }
        heartbeat_thread_.join();
    }
    lifecycle_lock.unlock();
    changeState(ConnectionState::Disconnected);
}

void HeartbeatManager::recordHeartbeatResponse() {
    last_response_time_us_ =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    reconnect_attempts_ = 0;
    changeState(ConnectionState::Connected);
}

ConnectionState HeartbeatManager::getState() const {
    return state_.load(std::memory_order_acquire);
}

bool HeartbeatManager::isConnected() const {
    return getState() == ConnectionState::Connected;
}

void HeartbeatManager::setHeartbeatCallback(SendHeartbeatCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    send_callback_ = std::move(callback);
}

void HeartbeatManager::setStateChangeCallback(StateChangeCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    state_callback_ = std::move(callback);
}

int64_t HeartbeatManager::getTimeSinceLastResponse() const {
    const auto now = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return (now - last_response_time_us_) / 1000;
}

uint32_t HeartbeatManager::getReconnectionAttempts() const {
    return reconnect_attempts_.load();
}

void HeartbeatManager::reset() {
    recordHeartbeatResponse();
}

void HeartbeatManager::forceReconnect() {
    reconnect_attempts_ = 0;
    changeState(ConnectionState::Reconnecting);
}

void HeartbeatManager::heartbeatLoop() {
    std::unique_lock<std::mutex> wait_lock(wait_mutex_);
    while (running_) {
        const int64_t since_response = getTimeSinceLastResponse();
        if (since_response > config_.heartbeat_timeout_ms &&
            state_ == ConnectionState::Connected) {
            if (reconnect_attempts_.load() < static_cast<uint32_t>(config_.max_retries)) {
                changeState(ConnectionState::Reconnecting);
            } else {
                changeState(ConnectionState::Failed);
            }
        }

        if (state_ == ConnectionState::Reconnecting) {
            const int64_t backoff_ms = calculateBackoffMs();
            wait_cv_.wait_for(wait_lock, std::chrono::milliseconds(backoff_ms),
                              [this] { return !running_.load(); });
            if (!running_) {
                break;
            }
            if (attemptReconnect()) {
                recordHeartbeatResponse();
            } else {
                ++reconnect_attempts_;
            }
            continue;
        }

        if (state_ == ConnectionState::Connected) {
            SendHeartbeatCallback callback;
            {
                std::lock_guard<std::mutex> lock(callback_mutex_);
                callback = send_callback_;
            }
            if (callback) {
                callback();
            }
        }

        wait_cv_.wait_for(wait_lock, std::chrono::milliseconds(config_.heartbeat_interval_ms),
                          [this] { return !running_.load(); });
    }
}

void HeartbeatManager::changeState(ConnectionState new_state) {
    const ConnectionState old_state = state_.exchange(new_state, std::memory_order_acq_rel);
    if (old_state == new_state) {
        return;
    }

    StateChangeCallback callback;
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        callback = state_callback_;
    }
    if (callback) {
        callback(old_state, new_state);
    }
    wait_cv_.notify_all();
}

int64_t HeartbeatManager::calculateBackoffMs() const {
    const uint32_t attempts = reconnect_attempts_.load();
    const double scaled = config_.initial_backoff_ms *
                          std::pow(config_.backoff_multiplier, static_cast<double>(attempts));
    if (!std::isfinite(scaled)) {
        return config_.max_backoff_ms;
    }
    return std::min(static_cast<int64_t>(scaled),
                    static_cast<int64_t>(config_.max_backoff_ms));
}

bool HeartbeatManager::attemptReconnect() {
    SendHeartbeatCallback callback;
    {
        std::lock_guard<std::mutex> lock(callback_mutex_);
        callback = send_callback_;
    }
    return callback && callback();
}
