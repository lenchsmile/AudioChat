#pragma once

#include <cstdint>
#include <chrono>
#include <deque>
#include <unordered_set>
#include <mutex>

/**
 * @struct NetworkMetrics
 * @brief Network performance metrics snapshot
 */
struct NetworkMetrics {
    double packet_loss_rate = 0.0;
    uint32_t total_packets_sent = 0;
    uint32_t total_packets_lost = 0;

    double rtt_ms = 0.0;
    double jitter_ms = 0.0;
    double min_rtt_ms = 0.0;
    double max_rtt_ms = 0.0;

    uint32_t reordered_packets = 0;
    double reorder_rate = 0.0;
    double estimated_bandwidth_kbps = 0.0;

    std::chrono::system_clock::time_point timestamp;
    bool is_connected = false;
};

/**
 * @brief Thread-safe network statistics collector.
 */
class NetworkMonitor {
public:
    NetworkMonitor();
    ~NetworkMonitor();

    void recordPacketSent(uint32_t sequence_number, size_t payload_size);
    void recordPacketReceived(uint32_t sequence_number, size_t payload_size,
                              uint64_t remote_timestamp);
    void recordPacketLoss(uint32_t sequence_number);

    NetworkMetrics getMetrics() const;
    double getPacketLossRate() const;
    double getRttMs() const;
    double getJitterMs() const;
    double getReorderRate() const;

    void reset();
    void setWindowSize(size_t window_size);
    void setJitterBufferAdaptation(bool enable);

private:
    struct PacketInfo {
        uint32_t sequence_number = 0;
        size_t payload_size = 0;
        std::chrono::steady_clock::time_point send_time;
        std::chrono::steady_clock::time_point receive_time;
        bool received = false;
        uint64_t remote_timestamp = 0;
    };

    struct RttSample {
        double rtt_ms = 0.0;
        std::chrono::steady_clock::time_point timestamp;
    };

    double calculatePacketLossRateLocked() const;
    double calculateRttMsLocked() const;
    double calculateJitterMsLocked() const;
    double calculateReorderRateLocked() const;
    double calculateBandwidthLocked() const;
    void pruneHistoryLocked();

    mutable std::mutex stats_mutex_;
    std::deque<PacketInfo> packet_history_;
    std::deque<RttSample> rtt_samples_;
    std::unordered_set<uint32_t> received_sequences_;

    uint32_t total_sent_ = 0;
    uint32_t total_received_ = 0;
    uint32_t total_unique_received_ = 0;
    uint32_t total_reordered_ = 0;
    uint32_t last_sequence_received_ = 0;
    bool has_received_sequence_ = false;

    size_t window_size_ = 100;
    bool jitter_buffer_adaptation_enabled_ = true;
};
