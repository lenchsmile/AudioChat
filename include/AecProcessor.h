#pragma once

#include "Result.h"
#include <cstdint>
#include <atomic>
#include <memory>
#include <optional>
#include <vector>

namespace AudioChat {

/**
 * @brief Configuration for Acoustic Echo Cancellation (AEC) processor
 * 
 * This structure holds all configuration parameters for the AEC processor.
 * All values should be validated before passing to AecProcessor.
 */
struct AecConfig {
    // Audio format
    int32_t sample_rate_hz = 48000;      ///< Sample rate in Hz (8000, 16000, 32000, 48000)
    int32_t num_channels = 1;            ///< Number of audio channels (1 or 2)
    int32_t frame_duration_ms = 10;      ///< Frame duration in ms (10 or 20, recommended: 10)
    
    // AEC settings
    bool enable_aec = true;              ///< Enable acoustic echo cancellation
    int32_t aec_suppression_level = 2;   ///< Reserved compatibility setting; current backend uses validated defaults
    int32_t stream_delay_ms = 0;          ///< Residual reverse-to-capture delay hint
    
    // Noise suppression
    bool enable_ns = true;               ///< Enable noise suppression
    int32_t ns_level = 1;                ///< NS level (0=Low, 1=Moderate, 2=High)
    
    // Automatic Gain Control
    bool enable_agc = true;              ///< Enable automatic gain control
    bool enable_agc_target_loudness = true; ///< Enable target loudness enhancement
    
    // Voice Activity Detection
    bool enable_vad = false;             ///< Enable voice activity detection
    
    /**
     * @brief Validate configuration values
     * @return Result with error code if validation fails
     */
    Result<void> Validate() const;
};

/**
 * @brief Audio frame structure for AEC processing
 * 
 * Represents a frame of audio data to be processed by AEC.
 * Data is expected in interleaved PCM format (16-bit signed).
 */
struct AudioFrame {
    std::vector<int16_t> data;           ///< Interleaved PCM audio data
    int32_t sample_count = 0;            ///< Number of samples in the frame
    int32_t channels = 1;                ///< Number of channels in this frame
    uint64_t timestamp_ms = 0;           ///< Timestamp in milliseconds (optional)
    
    /**
     * @brief Get size of data buffer in bytes
     * @return Number of bytes of audio data
     */
    size_t GetDataSizeBytes() const { return data.size() * sizeof(int16_t); }
};

/**
 * @brief Acoustic Echo Cancellation Processor
 * 
 * This class wraps the WebRTC audio processing module to provide
 * echo cancellation for audio chat applications.
 * 
 * Typical usage:
 *   1. Create AecConfig with desired settings
 *   2. Create AecProcessor with the config
 *   3. For each audio frame:
 *      a. Call ProcessReferenceStream() with speaker (far-end) signal
 *      b. Call ProcessCaptureStream() with microphone (near-end) signal
 *      c. ProcessCaptureStream() returns echo-cancelled audio
 */
class AecProcessor {
public:
    /**
     * @brief Create and initialize AEC processor
     * 
     * @param config Configuration for the AEC processor
     * @return Result containing the AecProcessor pointer on success, or error code on failure
     * 
     * Error codes:
     *   - AudioChatError::InvalidCodecConfig: Config validation failed
     *   - AudioChatError::AecInitFailed: Failed to initialize WebRTC module
     */
    static Result<std::unique_ptr<AecProcessor>> Create(const AecConfig& config);
    
    /**
     * @brief Destructor
     * 
     * Cleans up WebRTC audio processing resources
     */
    ~AecProcessor();
    
    // Delete copy operations
    AecProcessor(const AecProcessor&) = delete;
    AecProcessor& operator=(const AecProcessor&) = delete;
    
    AecProcessor(AecProcessor&&) = delete;
    AecProcessor& operator=(AecProcessor&&) = delete;
    
    /**
     * @brief Process reference signal (speaker/far-end audio)
     * 
     * This should be called with audio that is being played through speakers.
     * The AEC algorithm uses this as a reference to identify and remove echo
     * from the microphone input.
     * 
     * @param reference_frame Audio frame from speaker output
     * @return Result indicating success or error
     * 
     * Error codes:
     *   - AudioChatError::AecProcessFailed: Frame validation or processing error
     */
    Result<void> ProcessReferenceStream(const AudioFrame& reference_frame);
    
    /**
     * @brief Process capture signal (microphone/near-end audio)
     * 
     * This processes the microphone input and applies echo cancellation,
     * noise suppression, and gain control. The returned audio should have
     * reduced echo and improved quality.
     * 
     * @param capture_frame Audio frame from microphone input
     * @return Result containing processed audio or error code
     * 
     * Error codes:
     *   - AudioChatError::AecProcessFailed: Frame validation or processing error
     */
    Result<AudioFrame> ProcessCaptureStream(const AudioFrame& capture_frame);
    
    /**
     * @brief Enable or disable AEC processing
     * 
     * @param enable Whether to enable AEC
     * @return Result indicating success or error
     */
    Result<void> SetAecEnabled(bool enable);
    
    /**
     * @brief Set the reserved AEC suppression-level compatibility setting.
     *
     * The current WebRTC backend uses validated default AEC3 tuning. This
     * setter validates and stores the value for configuration compatibility;
     * it does not change suppression strength.
     *
     * @param level Suppression level (0=Low, 1=Moderate, 2=High)
     * @return Result indicating success or error
     * 
     * Error codes:
     *   - AudioChatError::InvalidArgument: Level out of range
     */
    Result<void> SetAecSuppressionLevel(int32_t level);

    /**
     * @brief Reset processor state after an underrun, device change or resync.
     */
    Result<void> Reset();
    
    /**
     * @brief Get current configuration
     * 
     * @return Current AecConfig
     */
    AecConfig GetConfig() const;

    /**
     * @brief True only when a real Audio Processing backend is active.
     */
    bool IsBackendActive() const;

    std::optional<double> GetEchoReturnLossEnhancement() const;
    std::optional<int32_t> GetAecDelayMs() const;
    
    /**
     * @brief Get statistics about processing
     * 
     * @return String with current processing statistics (for debugging)
     */
    std::string GetStatistics() const;

private:
    /**
     * @brief Private constructor - use Create() factory method
     * 
     * @param config Configuration for the AEC processor
     */
    explicit AecProcessor(const AecConfig& config);
    
    /**
     * @brief Internal initialization
     * 
     * @return Result indicating success or error
     */
    Result<void> Initialize();
    Result<void> ResetLocked();
    
    /**
     * @brief Validate audio frame
     * 
     * @param frame Frame to validate
     * @return Result indicating success or error
     */
    Result<void> ValidateAudioFrame(const AudioFrame& frame) const;
    
    // Configuration
    AecConfig config_;
    
    // State
    std::atomic<bool> is_initialized_{false};
    uint64_t frames_processed_ = 0;
    
    // WebRTC implementation is hidden behind a pimpl boundary.
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace AudioChat
