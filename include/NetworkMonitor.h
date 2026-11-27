#pragma once

#include <cstdint>
#include <chrono>
#include <deque>
#include <atomic>
#include <thread>
#include <mutex>
#include <memory>

/**
 * @struct NetworkMetrics
 * @brief Network performance metrics snapshot
 */
struct NetworkMetrics {
    // Loss statistics
    double packet_loss_rate = 0.0;        // %
    uint32_t total_packets_sent = 0;
    uint32_t total_packets_lost = 0;
    
    // Latency statistics
    double rtt_ms = 0.0;                  // Round-trip time in milliseconds
    double jitter_ms = 0.0;               // Jitter in milliseconds
    double min_rtt_ms = 0.0;
    double max_rtt_ms = 0.0;
    
    // Reordering
    uint32_t reordered_packets = 0;
    double reorder_rate = 0.0;            // %
    
    // Bandwidth
    double estimated_bandwidth_kbps = 0.0;
    
    // Timestamps
    std::chrono::system_clock::time_point timestamp;
    
    // Connection status
    bool is_connected = false;
};

/**
 * @class NetworkMonitor
 * @brief Monitor and collect network statistics
 * 
 * Features:
 * - Real-time packet loss calculation
 * - RTT estimation using timestamps
 * - Jitter measurement
 * - Reorder detection
 * - Bandwidth estimation
 * - Thread-safe access
 */
class NetworkMonitor {
public:
    NetworkMonitor();
    ~NetworkMonitor();
    
    /**
     * @brief Record sent packet
     * @param sequence_number Packet sequence number
     * @param payload_size Payload size in bytes
     */
    void recordPacketSent(uint32_t sequence_number, size_t payload_size);
    
    /**
     * @brief Record received packet
     * @param sequence_number Packet sequence number
     * @param payload_size Payload size in bytes
     * @param remote_timestamp Remote packet timestamp
     */
    void recordPacketReceived(uint32_t sequence_number, size_t payload_size, 
                             uint64_t remote_timestamp);
    
    /**
     * @brief Record packet loss
     * @param sequence_number Lost packet sequence number
     */
    void recordPacketLoss(uint32_t sequence_number);
    
    /**
     * @brief Get current network metrics
     * @return NetworkMetrics snapshot
     */
    NetworkMetrics getMetrics() const;
    
    /**
     * @brief Get packet loss rate percentage
     * @return Loss rate 0-100
     */
    double getPacketLossRate() const;
    
    /**
     * @brief Get estimated RTT in milliseconds
     * @return RTT milliseconds
     */
    double getRttMs() const;
    
    /**
     * @brief Get jitter in milliseconds
     * @return Jitter milliseconds
     */
    double getJitterMs() const;
    
    /**
     * @brief Get reorder rate percentage
     * @return Reorder rate 0-100
     */
    double getReorderRate() const;
    
    /**
     * @brief Reset all statistics
     */
    void reset();
    
    /**
     * @brief Set window size for statistics calculation
     * @param window_size Number of packets to consider
     */
    void setWindowSize(size_t window_size);
    
    /**
     * @brief Enable/disable jitter buffer adaptation hints
     * @param enable True to enable
     */
    void setJitterBufferAdaptation(bool enable);

private:
    struct PacketInfo {
        uint32_t sequence_number;
        size_t payload_size;
        std::chrono::system_clock::time_point send_time;
        std::chrono::system_clock::time_point receive_time;
        bool received = false;
        uint64_t remote_timestamp = 0;
    };
    
    struct RttSample {
        double rtt_ms;
        std::chrono::system_clock::time_point timestamp;
    };
    
    // Statistics
    mutable std::mutex stats_mutex_;
    std::deque<PacketInfo> packet_history_;
    std::deque<RttSample> rtt_samples_;
    
    uint32_t total_sent_ = 0;
    uint32_t total_received_ = 0;
    uint32_t total_lost_ = 0;
    uint32_t total_reordered_ = 0;
    
    uint32_t last_sequence_received_ = 0;
    
    size_t window_size_ = 100;
    bool jitter_buffer_adaptation_enabled_ = true;
    
    // Atomic metrics cache
    mutable std::atomic<double> cached_loss_rate_{0.0};
    mutable std::atomic<double> cached_rtt_ms_{0.0};
    mutable std::atomic<double> cached_jitter_ms_{0.0};
    
    // Helper methods
    void updateMetricsCache() const;
    double calculateRttMs(const PacketInfo& pkt) const;
    double calculateJitterMs() const;
    double calculateBandwidth() const;
    bool isReordered(uint32_t sequence_number);
};

