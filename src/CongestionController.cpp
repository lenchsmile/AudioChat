#include "CongestionController.h"
#include <algorithm>
#include <cmath>

CongestionController::CongestionController() = default;

CongestionController::~CongestionController() = default;

void CongestionController::updatePacketLossRate(double loss_rate) {
    if (!std::isfinite(loss_rate)) {
        return;
    }
    loss_rate = std::clamp(loss_rate, 0.0, 100.0);

    std::lock_guard<std::mutex> lock(state_mutex_);
    current_loss_rate_ = loss_rate;
    loss_history_.push_back(loss_rate);
    if (loss_history_.size() > HISTORY_SIZE) {
        loss_history_.pop_front();
    }
    
    congestion_level_ = calculateCongestionLevel();
}

void CongestionController::updateRtt(double rtt_ms) {
    if (!std::isfinite(rtt_ms) || rtt_ms < 0.0) {
        return;
    }

    std::lock_guard<std::mutex> lock(state_mutex_);
    current_rtt_ = rtt_ms;
    rtt_history_.push_back(rtt_ms);
    if (rtt_history_.size() > HISTORY_SIZE) {
        rtt_history_.pop_front();
    }
    
    congestion_level_ = calculateCongestionLevel();
}

BitrateRecommendation CongestionController::getRecommendation(uint32_t current_bitrate) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    
    BitrateRecommendation rec;
    rec.congestion_level = congestion_level_.load();
    
    double smoothed_loss = calculateSmoothedLossRate();
    double smoothed_rtt = calculateSmoothedRtt();
    
    // Calculate confidence based on how consistent the conditions are
    rec.confidence = std::min(1.0, static_cast<double>(loss_history_.size()) / 10.0);
    
    // Determine if we should adjust
    rec.should_reduce = (smoothed_loss > loss_threshold_) || 
                       (smoothed_rtt > rtt_threshold_);
    rec.should_increase = (smoothed_loss < 1.0) && (smoothed_rtt < 50.0);
    
    // Calculate recommended bitrate
    rec.recommended_bitrate_kbps = calculateRecommendedBitrate(
        rec.congestion_level, current_bitrate);
    
    return rec;
}

CongestionLevel CongestionController::getCongestionLevel() const {
    return congestion_level_.load();
}

void CongestionController::setBitrateLimits(uint32_t min_kbps, uint32_t max_kbps) {
    if (min_kbps == 0 || min_kbps > max_kbps) {
        return;
    }
    std::lock_guard<std::mutex> lock(state_mutex_);
    min_bitrate_kbps_ = min_kbps;
    max_bitrate_kbps_ = max_kbps;
}

void CongestionController::setThresholds(double loss_threshold, double rtt_threshold) {
    if (!std::isfinite(loss_threshold) || !std::isfinite(rtt_threshold) ||
        loss_threshold <= 0.0 || rtt_threshold <= 0.0) {
        return;
    }
    std::lock_guard<std::mutex> lock(state_mutex_);
    loss_threshold_ = loss_threshold;
    rtt_threshold_ = rtt_threshold;

    severe_loss_threshold_ = loss_threshold * 3.0;
    severe_rtt_threshold_ = rtt_threshold * 2.5;
    congestion_level_ = calculateCongestionLevel();
}

uint32_t CongestionController::getSendRateLimit() const {
    CongestionLevel level = congestion_level_.load();
    
    switch (level) {
        case CongestionLevel::None:
            return 1000;  // No limit
        case CongestionLevel::Mild:
            return 500;   // 50% reduction
        case CongestionLevel::Moderate:
            return 250;   // 75% reduction
        case CongestionLevel::Severe:
            return 100;   // 90% reduction
        default:
            return 1000;
    }
}

void CongestionController::reset() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    loss_history_.clear();
    rtt_history_.clear();
    current_loss_rate_ = 0.0;
    current_rtt_ = 0.0;
    congestion_level_ = CongestionLevel::None;
    current_bitrate_ = 32;
}

CongestionLevel CongestionController::calculateCongestionLevel() {
    double smoothed_loss = calculateSmoothedLossRate();
    double smoothed_rtt = calculateSmoothedRtt();
    
    // Check severe conditions first
    if (smoothed_loss >= severe_loss_threshold_ || smoothed_rtt >= severe_rtt_threshold_) {
        return CongestionLevel::Severe;
    }

    if (smoothed_loss >= loss_threshold_ * 2.0 || smoothed_rtt >= rtt_threshold_ * 1.5) {
        return CongestionLevel::Moderate;
    }

    if (smoothed_loss >= loss_threshold_ || smoothed_rtt >= rtt_threshold_) {
        return CongestionLevel::Mild;
    }
    
    return CongestionLevel::None;
}

double CongestionController::calculateSmoothedLossRate() const {
    if (loss_history_.empty()) {
        return 0.0;
    }
    
    // Exponential moving average with more weight on recent samples
    double smoothed = 0.0;
    double weight_sum = 0.0;
    
    for (size_t i = 0; i < loss_history_.size(); ++i) {
        // Weight increases for more recent samples
        double weight = 1.0 + (i / static_cast<double>(loss_history_.size()));
        smoothed += loss_history_[i] * weight;
        weight_sum += weight;
    }
    
    return smoothed / weight_sum;
}

double CongestionController::calculateSmoothedRtt() const {
    if (rtt_history_.empty()) {
        return 0.0;
    }
    
    // Similar smoothing as loss rate
    double smoothed = 0.0;
    double weight_sum = 0.0;
    
    for (size_t i = 0; i < rtt_history_.size(); ++i) {
        double weight = 1.0 + (i / static_cast<double>(rtt_history_.size()));
        smoothed += rtt_history_[i] * weight;
        weight_sum += weight;
    }
    
    return smoothed / weight_sum;
}

uint32_t CongestionController::calculateRecommendedBitrate(CongestionLevel level,
                                                           uint32_t current) {
    uint32_t recommended = current;
    
    switch (level) {
        case CongestionLevel::None:
            // Can increase slightly
            recommended = std::min(current + 2, max_bitrate_kbps_);
            break;
            
        case CongestionLevel::Mild:
            // Reduce by 10%
            recommended = static_cast<uint32_t>(current * 0.9);
            break;
            
        case CongestionLevel::Moderate:
            // Reduce by 25%
            recommended = static_cast<uint32_t>(current * 0.75);
            break;
            
        case CongestionLevel::Severe:
            // Reduce by 50%
            recommended = static_cast<uint32_t>(current * 0.5);
            break;
    }
    
    // Clamp to limits
    recommended = std::max(recommended, min_bitrate_kbps_);
    recommended = std::min(recommended, max_bitrate_kbps_);
    
    return recommended;
}

