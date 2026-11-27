#pragma once

#include "Result.h"
#include <cstdint>
#include <vector>
#include <deque>
#include <memory>

namespace AudioChat {

/**
 * @brief Reference signal buffer for AEC timing synchronization
 * 
 * This class manages buffering of the reference signal (speaker/far-end audio)
 * with delay compensation. The AEC algorithm needs synchronized reference and
 * capture signals. This buffer handles:
 * 
 * 1. Buffering reference signal (what's being played)
 * 2. Delay compensation (accounting for playback + acoustic path delays)
 * 3. Frame alignment (ensuring reference frames align with capture frames)
 * 
 * Typical delay components:
 * - Playback buffer: ~20ms (depends on device)
 * - Acoustic path: 0-20ms (distance, room acoustics)
 * - Total typical delay: 20-40ms
 */
class ReferenceSignalBuffer {
public:
    /**
     * @brief Configuration for reference signal buffering
     */
    struct Config {
        int32_t sample_rate_hz = 48000;    ///< Sample rate in Hz
        int32_t num_channels = 1;           ///< Number of channels
        int32_t frame_duration_ms = 10;     ///< Frame duration in ms
        int32_t delay_ms = 30;              ///< Delay to compensate (ms)
        int32_t buffer_duration_ms = 100;   ///< Total buffer size (ms)
        
        /**
         * @brief Validate configuration
         * @return Result with error if validation fails
         */
        Result<void> Validate() const;
    };

    /**
     * @brief Create a reference signal buffer
     * 
     * @param config Configuration for the buffer
     * @return Result containing the buffer on success, or error code
     */
    static Result<std::unique_ptr<ReferenceSignalBuffer>> Create(const Config& config);

    /**
     * @brief Destructor
     */
    ~ReferenceSignalBuffer() = default;

    // Delete copy operations
    ReferenceSignalBuffer(const ReferenceSignalBuffer&) = delete;
    ReferenceSignalBuffer& operator=(const ReferenceSignalBuffer&) = delete;

    // Allow move operations
    ReferenceSignalBuffer(ReferenceSignalBuffer&&) = default;
    ReferenceSignalBuffer& operator=(ReferenceSignalBuffer&&) = default;

    /**
     * @brief Add reference signal (speaker output) to buffer
     * 
     * This should be called when audio is being played. The buffer will
     * store this data for later retrieval with delay compensation.
     * 
     * @param data Audio data (interleaved PCM, 16-bit signed)
     * @param num_samples Number of samples (per channel)
     * @return Result indicating success or error
     * 
     * Error codes:
     *   - AudioChatError::BufferOverflow: Buffer full, data discarded
     *   - AudioChatError::InvalidArgument: Invalid parameters
     */
    Result<void> PushReferenceSignal(const int16_t* data, int32_t num_samples);

    /**
     * @brief Get delayed reference signal for AEC processing
     * 
     * This retrieves reference signal that has been delayed by the configured
     * delay_ms. This delay-compensated signal should be passed to the AEC
     * processor to ensure temporal alignment with the capture signal.
     * 
     * @param num_samples Number of samples to retrieve (per channel)
     * @return Result containing reference frame, or error if insufficient data
     * 
     * Error codes:
     *   - AudioChatError::BufferUnderflow: Insufficient buffered data
     *   - AudioChatError::InvalidArgument: Invalid parameters
     */
    struct ReferenceFrame {
        std::vector<int16_t> data;      ///< Reference signal data
        int32_t num_samples = 0;        ///< Number of samples per channel
        uint64_t timestamp_ms = 0;      ///< Timestamp when signal was recorded
    };
    
    Result<ReferenceFrame> GetDelayedReference(int32_t num_samples);

    /**
     * @brief Get current buffer fill level
     * 
     * @return Number of samples currently buffered (per channel)
     */
    int32_t GetBufferedSamples() const;

    /**
     * @brief Get buffer statistics for debugging
     * 
     * @return String with buffer statistics
     */
    std::string GetStatistics() const;

    /**
     * @brief Clear all buffered data
     * 
     * Useful for resynchronization or error recovery.
     */
    void Clear();

    /**
     * @brief Check if buffer has enough data
     * 
     * @param num_samples Number of samples needed
     * @return true if buffer has at least num_samples available
     */
    bool HasSufficientData(int32_t num_samples) const;

    /**
     * @brief Get configuration
     * 
     * @return Current configuration
     */
    const Config& GetConfig() const { return config_; }

private:
    /**
     * @brief Private constructor - use Create() factory method
     * 
     * @param config Configuration for the buffer
     */
    explicit ReferenceSignalBuffer(const Config& config);

    /**
     * @brief Internal initialization
     * 
     * @return Result indicating success or error
     */
    Result<void> Initialize();

    /**
     * @brief Calculate delay in samples from delay_ms
     * 
     * @return Number of samples for the configured delay
     */
    int32_t GetDelaySamples() const;

    /**
     * @brief Calculate maximum buffer size in samples
     * 
     * @return Number of samples for the configured buffer duration
     */
    int32_t GetMaxBufferSamples() const;

    // Configuration
    Config config_;

    // State
    bool is_initialized_ = false;
    uint64_t frames_pushed_ = 0;
    uint64_t frames_retrieved_ = 0;

    // Ring buffer for reference signal history
    // We need to keep a history to apply the delay
    std::deque<int16_t> buffer_;

    // Timestamp tracking for synchronization
    uint64_t last_push_timestamp_ms_ = 0;
};

} // namespace AudioChat
