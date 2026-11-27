#pragma once

#include <cstdint>
#include <atomic>
#include <deque>
#include <mutex>

/**
 * @enum CongestionLevel
 * @brief Network congestion severity
 */
enum class CongestionLevel {
    None,       // No congestion
    Mild,       // Slight congestion, minor adjustments
    Moderate,   // Moderate congestion, significant adjustments
    Severe      // Severe congestion, major restrictions
};

/**
 * @struct BitrateRecommendation
 * @brief Bitrate adjustment recommendation
 */
struct BitrateRecommendation {
    uint32_t recommended_bitrate_kbps = 0;  // Recommended bitrate in kbps
    CongestionLevel congestion_level = CongestionLevel::None;
    double confidence = 0.0;                 // 0.0 - 1.0
    bool should_reduce = false;
    bool should_increase = false;
};

/**
 * @class CongestionController
 * @brief Detect and respond to network congestion
 * 
 * Features:
 * - Packet loss rate monitoring
 * - RTT-based congestion detection
 * - Bitrate adjustment recommendations
 * - Send rate limiting
 * - Adaptive response strategy
 */
class CongestionController {
public:
    CongestionController();
    ~CongestionController();
    
    /**
     * @brief Update packet loss rate
     * @param loss_rate Loss rate percentage (0-100)
     */
    void updatePacketLossRate(double loss_rate);
    
    /**
     * @brief Update RTT estimate
     * @param rtt_ms Round-trip time in milliseconds
     */
    void updateRtt(double rtt_ms);
    
    /**
     * @brief Get bitrate recommendation
     * @param current_bitrate Current bitrate in kbps
     * @return BitrateRecommendation
     */
    BitrateRecommendation getRecommendation(uint32_t current_bitrate);
    
    /**
     * @brief Get current congestion level
     * @return CongestionLevel
     */
    CongestionLevel getCongestionLevel() const;
    
    /**
     * @brief Set bitrate limits
     * @param min_kbps Minimum bitrate
     * @param max_kbps Maximum bitrate
     */
    void setBitrateLimits(uint32_t min_kbps, uint32_t max_kbps);
    
    /**
     * @brief Set congestion thresholds
     * @param loss_threshold Loss rate threshold (%)
     * @param rtt_threshold RTT threshold (ms)
     */
    void setThresholds(double loss_threshold, double rtt_threshold);
    
    /**
     * @brief Get send rate limit
     * @return Maximum packets per second
     */
    uint32_t getSendRateLimit() const;
    
    /**
     * @brief Reset controller state
     */
    void reset();

private:
    // Configuration
    double loss_threshold_ = 5.0;           // % loss to trigger mild congestion
    double severe_loss_threshold_ = 15.0;   // % loss to trigger severe
    
    double rtt_threshold_ = 200.0;          // ms RTT to trigger mild
    double severe_rtt_threshold_ = 500.0;   // ms RTT to trigger severe
    
    uint32_t min_bitrate_kbps_ = 8;
    uint32_t max_bitrate_kbps_ = 128;
    
    // State
    mutable std::mutex state_mutex_;
    
    std::atomic<double> current_loss_rate_{0.0};
    std::atomic<double> current_rtt_{0.0};
    std::atomic<CongestionLevel> congestion_level_{CongestionLevel::None};
    
    // History for smoothing
    std::deque<double> loss_history_;
    std::deque<double> rtt_history_;
    static constexpr size_t HISTORY_SIZE = 20;
    
    // Bitrate state
    uint32_t current_bitrate_ = 32;
    int32_t bitrate_adjustment_step_ = 2;  // kbps per adjustment
    
    // Helper methods
    CongestionLevel calculateCongestionLevel();
    double calculateSmoothedLossRate() const;
    double calculateSmoothedRtt() const;
    uint32_t calculateRecommendedBitrate(CongestionLevel level, uint32_t current);
};

