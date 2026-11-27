#include "AecAudioHandler.h"
#include "Logger.h"
#include <cstring>
#include <sstream>

// Forward declaration - old AudioHandler is in C++ but not in namespace
class AudioHandler;

namespace AudioChat {

// ============================================================================
// Config Implementation
// ============================================================================

Result<void> AecAudioHandler::Config::Validate() const {
    if (!enable_aec) {
        // AEC disabled - no validation needed
        return Result<void>::ok();
    }

    // Validate AEC config if enabled
    auto aec_validation = aec_config.Validate();
    if (aec_validation.isError()) {
        return aec_validation;
    }

    // Validate buffer config
    auto buffer_validation = buffer_config.Validate();
    if (buffer_validation.isError()) {
        return buffer_validation;
    }

    // Verify configs are compatible (sample rate, channels)
    if (aec_config.sample_rate_hz != buffer_config.sample_rate_hz) {
        return Result<void>::error(
            AudioChatError::InvalidCodecConfig,
            "AEC and buffer sample rates must match"
        );
    }

    if (aec_config.num_channels != buffer_config.num_channels) {
        return Result<void>::error(
            AudioChatError::InvalidCodecConfig,
            "AEC and buffer channel counts must match"
        );
    }

    return Result<void>::ok();
}

// ============================================================================
// AecAudioHandler Implementation
// ============================================================================

Result<std::unique_ptr<AecAudioHandler>> AecAudioHandler::Create(
    std::unique_ptr<AudioHandler> audio_handler,
    const Config& config) {
    
    if (!audio_handler) {
        return Result<std::unique_ptr<AecAudioHandler>>::error(
            AudioChatError::InvalidArgument,
            "AudioHandler is null"
        );
    }

    // Validate config
    auto validation = config.Validate();
    if (validation.isError()) {
        LOG_ERROR("AecAudioHandler::Create: Config validation failed - {}", 
                  validation.message());
        return Result<std::unique_ptr<AecAudioHandler>>::error(
            validation.error(),
            validation.message()
        );
    }

    // Create handler
    auto handler = std::unique_ptr<AecAudioHandler>(
        new AecAudioHandler(std::move(audio_handler), config)
    );

    // Initialize
    auto init_result = handler->Initialize();
    if (init_result.isError()) {
        LOG_ERROR("AecAudioHandler::Create: Initialization failed - {}", 
                  init_result.message());
        return Result<std::unique_ptr<AecAudioHandler>>::error(
            init_result.error(),
            init_result.message()
        );
    }

    return Result<std::unique_ptr<AecAudioHandler>>::ok(handler.release());
}

AecAudioHandler::AecAudioHandler(std::unique_ptr<AudioHandler> audio_handler,
                                 const Config& config)
    : config_(config), audio_handler_(std::move(audio_handler)) {
    LOG_INFO("AecAudioHandler created: AEC {}", 
             config.enable_aec ? "enabled" : "disabled");
}

AecAudioHandler::~AecAudioHandler() {
    LOG_INFO("AecAudioHandler destroyed");
}

Result<void> AecAudioHandler::Initialize() {
    if (config_.enable_aec) {
        // Initialize AEC processor
        auto aec_result = AecProcessor::Create(config_.aec_config);
        if (aec_result.isError()) {
            LOG_ERROR("Failed to create AEC processor: {}", 
                      aec_result.message());
            return Result<void>::error(aec_result.error(), aec_result.message());
        }
        aec_processor_ = std::move(aec_result.value());

        // Initialize reference signal buffer
        auto buffer_result = ReferenceSignalBuffer::Create(config_.buffer_config);
        if (buffer_result.isError()) {
            LOG_ERROR("Failed to create reference signal buffer: {}", 
                      buffer_result.message());
            return Result<void>::error(buffer_result.error(), 
                                      buffer_result.message());
        }
        reference_buffer_ = std::move(buffer_result.value());

        aec_enabled_.store(true);
        LOG_INFO("AEC initialized successfully");
    }

    is_initialized_ = true;
    return Result<void>::ok();
}

Result<void> AecAudioHandler::EnableAec() {
    if (!config_.enable_aec) {
        // AEC was not configured at creation time
        LOG_WARN("AEC not configured - cannot enable dynamically");
        return Result<void>::error(
            AudioChatError::NotInitialized,
            "AEC not configured at initialization"
        );
    }

    if (aec_enabled_.load()) {
        LOG_INFO("AEC already enabled");
        return Result<void>::ok();
    }

    // Reset state for clean restart
    if (reference_buffer_) {
        reference_buffer_->Clear();
    }

    aec_enabled_.store(true);
    LOG_INFO("AEC enabled");
    return Result<void>::ok();
}

Result<void> AecAudioHandler::DisableAec() {
    if (!aec_enabled_.load()) {
        LOG_INFO("AEC already disabled");
        return Result<void>::ok();
    }

    aec_enabled_.store(false);
    if (reference_buffer_) {
        reference_buffer_->Clear();
    }

    LOG_INFO("AEC disabled");
    return Result<void>::ok();
}

Result<void> AecAudioHandler::ProcessReferenceSignal(const int16_t* data,
                                                     int32_t num_samples) {
    if (!aec_enabled_.load()) {
        // AEC disabled - nothing to do
        return Result<void>::ok();
    }

    if (!reference_buffer_) {
        return Result<void>::error(
            AudioChatError::NotInitialized,
            "Reference signal buffer not initialized"
        );
    }

    if (!data || num_samples <= 0) {
        return Result<void>::error(
            AudioChatError::InvalidArgument,
            "Invalid reference signal"
        );
    }

    auto result = reference_buffer_->PushReferenceSignal(data, num_samples);
    if (result.isOk()) {
        reference_frames_processed_++;
    } else {
        LOG_WARN("Failed to push reference signal: {}", result.message());
        aec_failures_++;
    }

    return result;
}

Result<void> AecAudioHandler::ProcessCaptureSignal(const int16_t* input,
                                                   int32_t num_samples,
                                                   int16_t* output) {
    if (!input || !output || num_samples <= 0) {
        return Result<void>::error(
            AudioChatError::InvalidArgument,
            "Invalid capture signal parameters"
        );
    }

    // If AEC disabled or not initialized, return input unchanged
    if (!aec_enabled_.load() || !aec_processor_ || !reference_buffer_) {
        std::memcpy(output, input, num_samples * config_.aec_config.num_channels * sizeof(int16_t));
        return Result<void>::ok();
    }

    // Check if we have enough buffered reference signal
    if (!reference_buffer_->HasSufficientData(num_samples)) {
        LOG_WARN("Insufficient reference signal buffered - returning unprocessed audio");
        std::memcpy(output, input, num_samples * config_.aec_config.num_channels * sizeof(int16_t));
        aec_failures_++;
        return Result<void>::ok();  // Not an error, just fallback
    }

    // Create AudioFrame for capture signal
    AudioFrame capture_frame;
    capture_frame.data.resize(num_samples * config_.aec_config.num_channels);
    std::memcpy(capture_frame.data.data(), input, 
                num_samples * config_.aec_config.num_channels * sizeof(int16_t));
    capture_frame.sample_count = num_samples;
    capture_frame.channels = config_.aec_config.num_channels;

    // Process through AEC
    auto aec_result = aec_processor_->ProcessCaptureStream(capture_frame);
    if (aec_result.isError()) {
        LOG_WARN("AEC processing failed: {} - returning unprocessed audio", 
                 aec_result.message());
        std::memcpy(output, input, num_samples * config_.aec_config.num_channels * sizeof(int16_t));
        aec_failures_++;
        return Result<void>::ok();  // Fallback, not error
    }

    // Copy processed audio to output
    AudioFrame processed = aec_result.value();
    std::memcpy(output, processed.data.data(),
                std::min(processed.data.size(), 
                        static_cast<size_t>(num_samples * config_.aec_config.num_channels)) * sizeof(int16_t));

    capture_frames_processed_++;
    return Result<void>::ok();
}

Result<void> AecAudioHandler::ReconfigureAec(const AecConfig& aec_config) {
    if (!aec_processor_) {
        return Result<void>::error(
            AudioChatError::NotInitialized,
            "AEC processor not initialized"
        );
    }

    auto validation = aec_config.Validate();
    if (validation.isError()) {
        return validation;
    }

    // Update AEC configuration
    // Note: AecProcessor doesn't have a public reconfigure method
    // For now, just update suppression level
    auto result = aec_processor_->SetAecSuppressionLevel(aec_config.aec_suppression_level);
    if (result.isOk()) {
        config_.aec_config = aec_config;
        LOG_INFO("AEC reconfigured: suppression level = {}", 
                 aec_config.aec_suppression_level);
    }

    return result;
}

std::string AecAudioHandler::GetAecStatistics() const {
    if (!aec_processor_) {
        return "AEC processor not initialized";
    }
    return aec_processor_->GetStatistics();
}

std::string AecAudioHandler::GetBufferStatistics() const {
    if (!reference_buffer_) {
        return "Reference signal buffer not initialized";
    }
    return reference_buffer_->GetStatistics();
}

Result<void> AecAudioHandler::ResetAec() {
    if (!reference_buffer_) {
        return Result<void>::error(
            AudioChatError::NotInitialized,
            "Reference signal buffer not initialized"
        );
    }

    reference_buffer_->Clear();
    reference_frames_processed_ = 0;
    capture_frames_processed_ = 0;
    aec_failures_ = 0;

    LOG_INFO("AEC state reset");
    return Result<void>::ok();
}

} // namespace AudioChat
