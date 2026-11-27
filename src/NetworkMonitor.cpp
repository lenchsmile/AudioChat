#include "NetworkMonitor.h"

#include <algorithm>
#include <cmath>

NetworkMonitor::NetworkMonitor() = default;
NetworkMonitor::~NetworkMonitor() = default;

void NetworkMonitor::recordPacketSent(uint32_t sequence_number, size_t payload_size) {
    std::lock_guard<std::mutex> lock(stats_mutex_);

    PacketInfo info;
    info.sequence_number = sequence_number;
    info.payload_size = payload_size;
    info.send_time = std::chrono::steady_clock::now();
    packet_history_.push_back(info);
    ++total_sent_;

    pruneHistoryLocked();
}

void NetworkMonitor::recordPacketReceived(uint32_t sequence_number, size_t payload_size,
                                          uint64_t remote_timestamp) {
    std::lock_guard<std::mutex> lock(stats_mutex_);

    const auto now = std::chrono::steady_clock::now();
    auto it = std::find_if(packet_history_.begin(), packet_history_.end(),
                           [sequence_number](const PacketInfo& info) {
                               return info.sequence_number == sequence_number;
                           });

    if (it != packet_history_.end() && !it->received) {
        it->received = true;
        it->payload_size = payload_size;
        it->receive_time = now;
        it->remote_timestamp = remote_timestamp;

        const double rtt_ms = std::chrono::duration<double, std::milli>(now - it->send_time).count();
        if (rtt_ms >= 0.0 && rtt_ms < 10000.0) {
            rtt_samples_.push_back({rtt_ms, now});
            while (rtt_samples_.size() > window_size_) {
                rtt_samples_.pop_front();
            }
        }
    }

    ++total_received_;
    if (received_sequences_.insert(sequence_number).second) {
        ++total_unique_received_;
    }

    if (has_received_sequence_ && sequence_number < last_sequence_received_) {
        ++total_reordered_;
    }
    last_sequence_received_ = std::max(last_sequence_received_, sequence_number);
    has_received_sequence_ = true;

    pruneHistoryLocked();
}

void NetworkMonitor::recordPacketLoss(uint32_t sequence_number) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    auto it = std::find_if(packet_history_.begin(), packet_history_.end(),
                           [sequence_number](const PacketInfo& info) {
                               return info.sequence_number == sequence_number;
                           });
    if (it != packet_history_.end()) {
        it->received = false;
    }
    // Loss is derived from sent minus uniquely acknowledged packets. This avoids
    // double counting when both a sequence gap and an explicit loss are observed.
}

NetworkMetrics NetworkMonitor::getMetrics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);

    NetworkMetrics metrics;
    metrics.packet_loss_rate = calculatePacketLossRateLocked();
    metrics.total_packets_sent = total_sent_;
    metrics.total_packets_lost = total_sent_ - std::min(total_sent_, total_unique_received_);
    metrics.rtt_ms = calculateRttMsLocked();
    metrics.jitter_ms = calculateJitterMsLocked();
    metrics.reordered_packets = total_reordered_;
    metrics.reorder_rate = calculateReorderRateLocked();
    metrics.estimated_bandwidth_kbps = calculateBandwidthLocked();
    metrics.timestamp = std::chrono::system_clock::now();
    metrics.is_connected = total_received_ > 0;

    if (!rtt_samples_.empty()) {
        metrics.min_rtt_ms = rtt_samples_.front().rtt_ms;
        metrics.max_rtt_ms = rtt_samples_.front().rtt_ms;
        for (const auto& sample : rtt_samples_) {
            metrics.min_rtt_ms = std::min(metrics.min_rtt_ms, sample.rtt_ms);
            metrics.max_rtt_ms = std::max(metrics.max_rtt_ms, sample.rtt_ms);
        }
    }

    return metrics;
}

double NetworkMonitor::getPacketLossRate() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return calculatePacketLossRateLocked();
}

double NetworkMonitor::getRttMs() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return calculateRttMsLocked();
}

double NetworkMonitor::getJitterMs() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return calculateJitterMsLocked();
}

double NetworkMonitor::getReorderRate() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return calculateReorderRateLocked();
}

void NetworkMonitor::reset() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    packet_history_.clear();
    rtt_samples_.clear();
    received_sequences_.clear();
    total_sent_ = 0;
    total_received_ = 0;
    total_unique_received_ = 0;
    total_reordered_ = 0;
    last_sequence_received_ = 0;
    has_received_sequence_ = false;
}

void NetworkMonitor::setWindowSize(size_t window_size) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    window_size_ = std::max<size_t>(1, window_size);
    pruneHistoryLocked();
}

void NetworkMonitor::setJitterBufferAdaptation(bool enable) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    jitter_buffer_adaptation_enabled_ = enable;
}

double NetworkMonitor::calculatePacketLossRateLocked() const {
    if (total_sent_ == 0) {
        return 0.0;
    }
    const uint32_t acknowledged = std::min(total_sent_, total_unique_received_);
    return (static_cast<double>(total_sent_ - acknowledged) / total_sent_) * 100.0;
}

double NetworkMonitor::calculateRttMsLocked() const {
    if (rtt_samples_.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (const auto& sample : rtt_samples_) {
        sum += sample.rtt_ms;
    }
    return sum / rtt_samples_.size();
}

double NetworkMonitor::calculateJitterMsLocked() const {
    if (rtt_samples_.size() < 2) {
        return 0.0;
    }
    double sum = 0.0;
    for (const auto& sample : rtt_samples_) {
        sum += sample.rtt_ms;
    }
    const double mean = sum / rtt_samples_.size();
    double variance = 0.0;
    for (const auto& sample : rtt_samples_) {
        const double diff = sample.rtt_ms - mean;
        variance += diff * diff;
    }
    return std::sqrt(variance / rtt_samples_.size());
}

double NetworkMonitor::calculateReorderRateLocked() const {
    if (total_received_ == 0) {
        return 0.0;
    }
    return (static_cast<double>(total_reordered_) / total_received_) * 100.0;
}

double NetworkMonitor::calculateBandwidthLocked() const {
    if (rtt_samples_.size() < 2) {
        return 0.0;
    }
    const auto elapsed = rtt_samples_.back().timestamp - rtt_samples_.front().timestamp;
    const double seconds = std::chrono::duration<double>(elapsed).count();
    if (seconds < 0.001) {
        return 0.0;
    }
    size_t bytes = 0;
    for (const auto& packet : packet_history_) {
        if (packet.received) {
            bytes += packet.payload_size;
        }
    }
    return (static_cast<double>(bytes) * 8.0 / 1000.0) / seconds;
}

void NetworkMonitor::pruneHistoryLocked() {
    while (packet_history_.size() > window_size_ * 2) {
        received_sequences_.erase(packet_history_.front().sequence_number);
        packet_history_.pop_front();
    }
}
