#pragma once

#include "Result.h"
#include <opus/opus.h>
#include <cstdint>
#include <memory>
#include <string>

// Forward declaration of old Codec class
class Codec;

namespace AudioChat {

/**
 * @brief Codec optimization parameters for Opus encoder
 * 
 * This structure manages codec parameters for optimal voice communication
 * in audio chat applications, particularly when used with AEC.
 */
struct CodecOptimizationConfig {
    // Bitrate configuration
    int32_t bitrate_bps = 24000;            ///< Target bitrate in bps (8000-128000)
    bool enable_vbr = true;                 ///< Variable Bitrate (adapts to content)
    bool enable_vbr_constraint = false;     ///< Constrain VBR to avoid spikes
    
    // DTX (Discontinuous Transmission) - critical for VoIP
    bool enable_dtx = true;                 ///< Enable DTX (save bandwidth during silence)
    
    // Redundancy and error resilience
    bool enable_fec = true;                 ///< Forward Error Correction
    int32_t packet_loss_percent = 10;       ///< Expected packet loss (0-100%)
    
    // Quality parameters
    int32_t complexity = 5;                 ///< Complexity 0-10 (higher = better, slower)
    int32_t max_bandwidth = OPUS_BANDWIDTH_FULLBAND;  ///< Max frequency bandwidth
    
    // Adaptive bitrate configuration
    bool enable_adaptive_bitrate = true;    ///< Enable automatic bitrate adjustment
    int32_t min_bitrate_bps = 8000;         ///< Minimum bitrate (bps)
    int32_t max_bitrate_bps = 128000;       ///< Maximum bitrate (bps)
    
    /**
     * @brief Validate configuration
     * @return Result with error if validation fails
     */
    Result<void> Validate() const;
};

/**
 * @brief Codec optimizer for Opus encoder configuration
 * 
 * This class provides easy management of Opus codec parameters to optimize
 * for voice chat with AEC. Key features:
 * 
 * 1. **DTX (Discontinuous Transmission)**
 *    - Reduces bandwidth during silence
 *    - Critical for VoIP efficiency
 *    - Works with AEC to detect voice pauses
 * 
 * 2. **Variable Bitrate (VBR)**
 *    - Adapts bitrate to content complexity
 *    - Provides better quality at lower average bitrate
 *    - Optional constraining to avoid bitrate spikes
 * 
 * 3. **Adaptive Bitrate**
 *    - Automatic adjustment based on network/hardware
 *    - Helps maintain call quality in variable conditions
 *    - Works with AEC to respond to echo conditions
 * 
 * 4. **Error Correction**
 *    - FEC (Forward Error Correction)
 *    - Packet loss compensation
 *    - Improves reliability on poor networks
 */
class CodecOptimizer {
public:
    /**
     * @brief Create codec optimizer
     * 
     * @param codec Existing Codec instance
     * @param config Optimization configuration
     * @return Result containing optimizer on success
     * 
     * Error codes:
     *   - AudioChatError::InvalidCodecConfig: Config validation failed
     *   - AudioChatError::CodecInitFailed: Failed to apply settings
     */
    static Result<std::unique_ptr<CodecOptimizer>> Create(
        Codec* codec,
        const CodecOptimizationConfig& config
    );

    /**
     * @brief Destructor
     */
    ~CodecOptimizer();

    // Delete copy operations
    CodecOptimizer(const CodecOptimizer&) = delete;
    CodecOptimizer& operator=(const CodecOptimizer&) = delete;

    // Allow move operations
    CodecOptimizer(CodecOptimizer&&) = default;
    CodecOptimizer& operator=(CodecOptimizer&&) = default;

    /**
     * @brief Enable DTX (Discontinuous Transmission)
     * 
     * DTX detects silence/pause in speech and transmits smaller packets
     * or no packets during these periods, significantly reducing bandwidth.
     * Critical optimization for VoIP.
     * 
     * @param enable Whether to enable DTX
     * @return Result indicating success or error
     */
    Result<void> SetDtxEnabled(bool enable);

    /**
     * @brief Check if DTX is enabled
     * 
     * @return true if DTX is active
     */
    bool IsDtxEnabled() const { return config_.enable_dtx; }

    /**
     * @brief Set bitrate (can be changed dynamically)
     * 
     * Changes the target bitrate for encoding. This can be called
     * during active encoding to adapt to network conditions.
     * 
     * @param bitrate_bps Target bitrate in bits per second (8000-128000)
     * @return Result indicating success or error
     * 
     * Error codes:
     *   - AudioChatError::InvalidArgument: Bitrate out of range
     */
    Result<void> SetBitrate(int32_t bitrate_bps);

    /**
     * @brief Get current bitrate
     * 
     * @return Current bitrate in bps
     */
    int32_t GetBitrate() const { return config_.bitrate_bps; }

    /**
     * @brief Enable/disable Variable Bitrate (VBR)
     * 
     * VBR adapts the bitrate based on content complexity:
     * - High complexity sections → higher bitrate
     * - Low complexity (silence) → lower bitrate
     * 
     * Results in better average quality at similar bitrate.
     * 
     * @param enable Whether to enable VBR
     * @return Result indicating success or error
     */
    Result<void> SetVbrEnabled(bool enable);

    /**
     * @brief Check if VBR is enabled
     * 
     * @return true if VBR is active
     */
    bool IsVbrEnabled() const { return config_.enable_vbr; }

    /**
     * @brief Set complexity level
     * 
     * Higher complexity provides better quality but uses more CPU.
     * 
     * @param complexity Level 0-10 (0=fast, 10=best)
     * @return Result indicating success or error
     */
    Result<void> SetComplexity(int32_t complexity);

    /**
     * @brief Get current complexity
     * 
     * @return Current complexity level
     */
    int32_t GetComplexity() const { return config_.complexity; }

    /**
     * @brief Set packet loss expectation
     * 
     * Tells encoder to expect this percentage of packet loss,
     * triggering FEC to protect against it.
     * 
     * @param loss_percent Expected loss percentage (0-100)
     * @return Result indicating success or error
     */
    Result<void> SetPacketLossPercent(int32_t loss_percent);

    /**
     * @brief Get statistics about optimization
     * 
     * @return String with current optimization statistics
     */
    std::string GetStatistics() const;

    /**
     * @brief Get configuration
     * 
     * @return Current configuration
     */
    const CodecOptimizationConfig& GetConfig() const { return config_; }

private:
    /**
     * @brief Private constructor - use Create() factory
     */
    CodecOptimizer(Codec* codec, const CodecOptimizationConfig& config);

    /**
     * @brief Apply all configuration to encoder
     */
    Result<void> ApplyConfiguration();

    /**
     * @brief Apply single setting to Opus encoder
     */
    Result<void> ApplySetting(int32_t request, int32_t value);

    // Configuration
    CodecOptimizationConfig config_;

    // State
    bool is_initialized_ = false;
    uint64_t settings_applied_ = 0;
    uint64_t bitrate_changes_ = 0;

    // Reference to codec (not owned)
    Codec* codec_ = nullptr;
};

} // namespace AudioChat
