#pragma once

#include "Result.h"
#include "AecProcessor.h"
#include "ReferenceSignalBuffer.h"
#include <memory>
#include <atomic>
#include <cstdint>

// Forward declaration of old AudioHandler
class AudioHandler;

namespace AudioChat {

/**
 * @brief Audio Handler wrapper with AEC (Acoustic Echo Cancellation) support
 * 
 * This class wraps the existing AudioHandler to add WebRTC-based echo
 * cancellation while maintaining backward compatibility. The integration
 * works as follows:
 * 
 * 1. Reference Signal (Speaker/Far-end Audio):
 *    - Captured in play() method
 *    - Buffered with delay compensation in ReferenceSignalBuffer
 * 
 * 2. Capture Signal (Microphone/Near-end Audio):
 *    - Processed through AecProcessor
 *    - Uses delayed reference signal for echo cancellation
 *    - Original audio path preserved if AEC fails
 * 
 * 3. Configuration:
 *    - AEC can be enabled/disabled dynamically
 *    - Graceful fallback to original audio path
 *    - Zero impact on existing code when disabled
 * 
 * @note This is a HIGH-RISK component that modifies audio processing.
 *       All changes are carefully isolated and testable.
 */
class AecAudioHandler {
public:
    /**
     * @brief Configuration for AEC Audio Handler
     */
    struct Config {
        // AEC Feature control
        bool enable_aec = false;                ///< Enable AEC processing
        
        // AEC Processor config (if enabled)
        AecConfig aec_config;                   ///< AEC processor configuration
        
        // Reference signal buffer config (if AEC enabled)
        ReferenceSignalBuffer::Config buffer_config;
        
        /**
         * @brief Validate configuration
         * @return Result with error if validation fails
         */
        Result<void> Validate() const;
    };

    /**
     * @brief Create AecAudioHandler wrapper
     * 
     * @param audio_handler Existing AudioHandler instance (takes ownership)
     * @param config Configuration for AEC
     * @return Result containing the wrapper on success
     * 
     * Error codes:
     *   - AudioChatError::InvalidCodecConfig: Config validation failed
     *   - AudioChatError::AecInitFailed: AEC initialization failed
     */
    static Result<std::unique_ptr<AecAudioHandler>> Create(
        std::unique_ptr<AudioHandler> audio_handler,
        const Config& config
    );

    /**
     * @brief Destructor
     */
    ~AecAudioHandler();

    // Delete copy operations
    AecAudioHandler(const AecAudioHandler&) = delete;
    AecAudioHandler& operator=(const AecAudioHandler&) = delete;

    // Allow move operations
    AecAudioHandler(AecAudioHandler&&) = default;
    AecAudioHandler& operator=(AecAudioHandler&&) = default;

    /**
     * @brief Get underlying AudioHandler (for existing code compatibility)
     * 
     * @return Pointer to underlying AudioHandler
     * @note Use this only if you need direct access to old API
     */
    AudioHandler* GetAudioHandler() const { return audio_handler_.get(); }

    /**
     * @brief Enable AEC processing
     * 
     * Can be called dynamically at runtime.
     * 
     * @return Result indicating success or error
     */
    Result<void> EnableAec();

    /**
     * @brief Disable AEC processing
     * 
     * When disabled, audio passes through without AEC processing
     * (original audio path preserved).
     * 
     * @return Result indicating success or error
     */
    Result<void> DisableAec();

    /**
     * @brief Check if AEC is currently enabled
     * 
     * @return true if AEC is active
     */
    bool IsAecEnabled() const { return aec_enabled_.load(); }

    /**
     * @brief Process reference signal (speaker output for AEC)
     * 
     * This should be called from the play() path with speaker audio.
     * The audio is buffered for use by the AEC processor to identify
     * and remove echo from the microphone input.
     * 
     * @param data Audio data (16-bit signed PCM)
     * @param num_samples Number of samples (per channel)
     * @return Result indicating success or error
     * 
     * @note This is typically called internally, not by end users
     */
    Result<void> ProcessReferenceSignal(const int16_t* data, int32_t num_samples);

    /**
     * @brief Process capture signal (microphone input) with AEC
     * 
     * Applies echo cancellation to microphone input using the buffered
     * reference signal. If AEC is disabled or insufficient buffered data,
     * returns original audio unchanged.
     * 
     * @param input Microphone audio (16-bit signed PCM)
     * @param num_samples Number of samples (per channel)
     * @param output Output buffer for processed audio (same size as input)
     * @return Result indicating success or error
     * 
     * @note Caller must ensure output buffer is large enough
     * @note If AEC fails, original audio is returned unchanged
     */
    Result<void> ProcessCaptureSignal(const int16_t* input, int32_t num_samples,
                                      int16_t* output);

    /**
     * @brief Reconfigure AEC parameters at runtime
     * 
     * @param aec_config New AEC configuration
     * @return Result indicating success or error
     */
    Result<void> ReconfigureAec(const AecConfig& aec_config);

    /**
     * @brief Get AEC statistics for debugging
     * 
     * @return String with current statistics
     */
    std::string GetAecStatistics() const;

    /**
     * @brief Get buffer statistics for debugging
     * 
     * @return String with buffer statistics
     */
    std::string GetBufferStatistics() const;

    /**
     * @brief Reset AEC state (synchronize after underrun/overrun)
     * 
     * Clears all buffered reference signal and resets AEC processor.
     * Useful for resynchronization after audio glitches.
     * 
     * @return Result indicating success or error
     */
    Result<void> ResetAec();

    /**
     * @brief Get configuration
     * 
     * @return Current configuration
     */
    const Config& GetConfig() const { return config_; }

private:
    /**
     * @brief Private constructor - use Create() factory method
     */
    explicit AecAudioHandler(std::unique_ptr<AudioHandler> audio_handler,
                             const Config& config);

    /**
     * @brief Internal initialization
     */
    Result<void> Initialize();

    // Configuration
    Config config_;

    // State
    bool is_initialized_ = false;
    std::atomic<bool> aec_enabled_{false};
    uint64_t reference_frames_processed_ = 0;
    uint64_t capture_frames_processed_ = 0;
    uint64_t aec_failures_ = 0;

    // Underlying audio handler (old API)
    std::unique_ptr<AudioHandler> audio_handler_;

    // AEC components (lazy-initialized if AEC enabled)
    std::unique_ptr<AecProcessor> aec_processor_;
    std::unique_ptr<ReferenceSignalBuffer> reference_buffer_;
};

} // namespace AudioChat
