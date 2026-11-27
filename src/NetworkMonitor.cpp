#include "NetworkMonitor.h"
#include <algorithm>
#include <cmath>

NetworkMonitor::NetworkMonitor() 
    : window_size_(100), jitter_buffer_adaptation_enabled_(true) {
}

NetworkMonitor::~NetworkMonitor() = default;

void NetworkMonitor::recordPacketSent(uint32_t sequence_number, size_t payload_size) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    PacketInfo info;
    info.sequence_number = sequence_number;
    info.payload_size = payload_size;
    info.send_time = std::chrono::system_clock::now();
    info.received = false;
    
    packet_history_.push_back(info);
    total_sent_++;
    
    // Keep window size bounded
    while (packet_history_.size() > window_size_ * 2) {
        packet_history_.pop_front();
    }
    
    updateMetricsCache();
}

void NetworkMonitor::recordPacketReceived(uint32_t sequence_number, size_t payload_size,
                                          uint64_t remote_timestamp) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    // Find matching sent packet
    auto it = std::find_if(packet_history_.begin(), packet_history_.end(),
        [sequence_number](const PacketInfo& info) {
            return info.sequence_number == sequence_number;
        });
    
    if (it != packet_history_.end()) {
        it->received = true;
        it->receive_time = std::chrono::system_clock::now();
        it->remote_timestamp = remote_timestamp;
        
        // Calculate RTT
        double rtt_ms = calculateRttMs(*it);
        if (rtt_ms > 0 && rtt_ms < 10000) {  // Sanity check
            RttSample sample;
            sample.rtt_ms = rtt_ms;
            sample.timestamp = std::chrono::system_clock::now();
            rtt_samples_.push_back(sample);
            
            // Keep window bounded
            while (rtt_samples_.size() > window_size_) {
                rtt_samples_.pop_front();
            }
        }
    }
    
    total_received_++;
    
    // Check for reordering
    if (sequence_number < last_sequence_received_ && last_sequence_received_ > 0) {
        total_reordered_++;
    }
    last_sequence_received_ = std::max(last_sequence_received_, sequence_number);
    
    updateMetricsCache();
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
    
    total_lost_++;
    updateMetricsCache();
}

NetworkMetrics NetworkMonitor::getMetrics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    NetworkMetrics metrics;
    metrics.packet_loss_rate = getPacketLossRate();
    metrics.rtt_ms = getRttMs();
    metrics.jitter_ms = getJitterMs();
    metrics.reorder_rate = getReorderRate();
    metrics.total_packets_sent = total_sent_;
    metrics.total_packets_lost = total_lost_;
    metrics.reordered_packets = total_reordered_;
    metrics.timestamp = std::chrono::system_clock::now();
    metrics.is_connected = total_received_ > 0;
    
    // Bandwidth estimation
    metrics.estimated_bandwidth_kbps = calculateBandwidth();
    
    return metrics;
}

double NetworkMonitor::getPacketLossRate() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    if (total_sent_ == 0) {
        return 0.0;
    }
    
    return (static_cast<double>(total_lost_) / total_sent_) * 100.0;
}

double NetworkMonitor::getRttMs() const {
    if (rtt_samples_.empty()) {
        return 0.0;
    }
    
    // Return average of recent samples
    double sum = 0.0;
    for (const auto& sample : rtt_samples_) {
        sum += sample.rtt_ms;
    }
    
    return sum / rtt_samples_.size();
}

double NetworkMonitor::getJitterMs() const {
    return calculateJitterMs();
}

double NetworkMonitor::getReorderRate() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    if (total_received_ == 0) {
        return 0.0;
    }
    
    return (static_cast<double>(total_reordered_) / total_received_) * 100.0;
}

void NetworkMonitor::reset() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    packet_history_.clear();
    rtt_samples_.clear();
    total_sent_ = 0;
    total_received_ = 0;
    total_lost_ = 0;
    total_reordered_ = 0;
    last_sequence_received_ = 0;
    
    cached_loss_rate_ = 0.0;
    cached_rtt_ms_ = 0.0;
    cached_jitter_ms_ = 0.0;
}

void NetworkMonitor::setWindowSize(size_t window_size) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    window_size_ = window_size;
}

void NetworkMonitor::setJitterBufferAdaptation(bool enable) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    jitter_buffer_adaptation_enabled_ = enable;
}

void NetworkMonitor::updateMetricsCache() const {
    cached_loss_rate_ = getPacketLossRate();
    cached_rtt_ms_ = getRttMs();
    cached_jitter_ms_ = calculateJitterMs();
}

double NetworkMonitor::calculateRttMs(const PacketInfo& pkt) const {
    if (!pkt.received) {
        return -1.0;
    }
    
    auto duration = pkt.receive_time - pkt.send_time;
    return std::chrono::duration<double, std::milli>(duration).count();
}

double NetworkMonitor::calculateJitterMs() const {
    if (rtt_samples_.size() < 2) {
        return 0.0;
    }
    
    // Calculate mean
    double sum = 0.0;
    for (const auto& sample : rtt_samples_) {
        sum += sample.rtt_ms;
    }
    double mean = sum / rtt_samples_.size();
    
    // Calculate variance
    double variance = 0.0;
    for (const auto& sample : rtt_samples_) {
        double diff = sample.rtt_ms - mean;
        variance += diff * diff;
    }
    variance /= rtt_samples_.size();
    
    // Standard deviation is jitter
    return std::sqrt(variance);
}

double NetworkMonitor::calculateBandwidth() const {
    if (rtt_samples_.size() < 2) {
        return 0.0;
    }
    
    // Simple estimation: total bytes received / time elapsed
    auto oldest_time = rtt_samples_.front().timestamp;
    auto newest_time = rtt_samples_.back().timestamp;
    
    auto duration = newest_time - oldest_time;
    double elapsed_seconds = std::chrono::duration<double>(duration).count();
    
    if (elapsed_seconds < 0.001) {
        return 0.0;
    }
    
    // Calculate total payload bytes in window
    size_t total_bytes = 0;
    for (const auto& pkt : packet_history_) {
        if (pkt.received) {
            total_bytes += pkt.payload_size;
        }
    }
    
    // Convert to kbps
    double bits = total_bytes * 8.0;
    return (bits / 1000.0) / elapsed_seconds;
}

bool NetworkMonitor::isReordered(uint32_t sequence_number) {
    return sequence_number < last_sequence_received_;
}

