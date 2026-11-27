#include "AecProcessor.h"
#include "Logger.h"
#include <sstream>

#ifdef HAS_WEBRTC_AEC
#include "webrtc/modules/audio_processing/include/audio_processing.h"
#endif

namespace AudioChat {

// ============================================================================
// AecConfig Implementation
// ============================================================================

Result<void> AecConfig::Validate() const {
    // Validate sample rate
    if (sample_rate_hz != 8000 && sample_rate_hz != 16000 && 
        sample_rate_hz != 32000 && sample_rate_hz != 48000) {
        return Result<void>::error(
            AudioChatError::InvalidCodecConfig,
            "Invalid sample rate: " + std::to_string(sample_rate_hz) + 
            " Hz (supported: 8000, 16000, 32000, 48000)"
        );
    }
    
    // Validate channels
    if (num_channels < 1 || num_channels > 2) {
        return Result<void>::error(
            AudioChatError::InvalidCodecConfig,
            "Invalid number of channels: " + std::to_string(num_channels) + 
            " (must be 1 or 2)"
        );
    }
    
    // Validate frame duration
    if (frame_duration_ms != 10 && frame_duration_ms != 20) {
        return Result<void>::error(
            AudioChatError::InvalidCodecConfig,
            "Invalid frame duration: " + std::to_string(frame_duration_ms) + 
            " ms (must be 10 or 20)"
        );
    }
    
    // Validate AEC suppression level
    if (aec_suppression_level < 0 || aec_suppression_level > 2) {
        return Result<void>::error(
            AudioChatError::AecInitFailed,
            "Invalid AEC suppression level: " + std::to_string(aec_suppression_level) + 
            " (must be 0, 1, or 2)"
        );
    }
    
    // Validate NS level
    if (ns_level < 0 || ns_level > 2) {
        return Result<void>::error(
            AudioChatError::AecInitFailed,
            "Invalid noise suppression level: " + std::to_string(ns_level) + 
            " (must be 0, 1, or 2)"
        );
    }
    
    return Result<void>::ok();
}

// ============================================================================
// AecProcessor Implementation
// ============================================================================

Result<std::unique_ptr<AecProcessor>> AecProcessor::Create(const AecConfig& config) {
    // Validate config
    auto validation_result = config.Validate();
    if (validation_result.isError()) {
        LOG_ERROR("AecProcessor::Create: Config validation failed - {}", validation_result.message());
        return Result<std::unique_ptr<AecProcessor>>::error(
            validation_result.error(),
            validation_result.message()
        );
    }
    
    // Create processor instance
    auto processor = std::unique_ptr<AecProcessor>(new AecProcessor(config));
    
    // Initialize
    auto init_result = processor->Initialize();
    if (init_result.isError()) {
        LOG_ERROR("AecProcessor::Create: Initialization failed - {}", init_result.message());
        return Result<std::unique_ptr<AecProcessor>>::error(
            init_result.error(),
            init_result.message()
        );
    }
    
    return Result<std::unique_ptr<AecProcessor>>::ok(processor.release());
}

AecProcessor::AecProcessor(const AecConfig& config) : config_(config) {
    LOG_INFO("AecProcessor created with config: {}Hz, {} channels",
             config.sample_rate_hz, config.num_channels);
}

Result<void> AecProcessor::Initialize() {
#ifndef HAS_WEBRTC_AEC
    LOG_WARN("WebRTC Audio Processing not available - AEC disabled");
    is_initialized_ = true;
    return Result<void>::ok();
#else
    // TODO: Initialize WebRTC Audio Processing module
    // This requires WebRTC library to be installed
    
    is_initialized_ = true;
    LOG_INFO("AecProcessor initialized successfully");
    return Result<void>::ok();
#endif
}

Result<void> AecProcessor::ValidateAudioFrame(const AudioFrame& frame) const {
    if (frame.data.empty()) {
        return Result<void>::error(
            AudioChatError::AecProcessFailed,
            "Audio frame data is empty"
        );
    }
    
    if (frame.sample_count <= 0) {
        return Result<void>::error(
            AudioChatError::AecProcessFailed,
            "Invalid sample count: " + std::to_string(frame.sample_count)
        );
    }
    
    if (frame.channels < 1 || frame.channels > 2) {
        return Result<void>::error(
            AudioChatError::AecProcessFailed,
            "Invalid number of channels in frame: " + std::to_string(frame.channels)
        );
    }
    
    // Expected size: sample_count * channels * sizeof(int16_t)
    size_t expected_size = frame.sample_count * frame.channels;
    if (frame.data.size() != expected_size) {
        return Result<void>::error(
            AudioChatError::AecProcessFailed,
            "Frame size mismatch: got " + std::to_string(frame.data.size()) +
            " samples, expected " + std::to_string(expected_size)
        );
    }
    
    return Result<void>::ok();
}

Result<void> AecProcessor::ProcessReferenceStream(const AudioFrame& reference_frame) {
    if (!is_initialized_) {
        return Result<void>::error(
            AudioChatError::AecProcessFailed,
            "AEC processor not initialized"
        );
    }
    
    auto validation = ValidateAudioFrame(reference_frame);
    if (validation.isError()) {
        LOG_ERROR("ProcessReferenceStream: {}", validation.message());
        return validation;
    }
    
#ifdef HAS_WEBRTC_AEC
    // TODO: Pass frame to WebRTC Audio Processing
    // apm_->AnalyzeReverseStream(...)
#endif
    
    frames_processed_++;
    return Result<void>::ok();
}

Result<AudioFrame> AecProcessor::ProcessCaptureStream(const AudioFrame& capture_frame) {
    if (!is_initialized_) {
        return Result<AudioFrame>::error(
            AudioChatError::AecProcessFailed,
            "AEC processor not initialized"
        );
    }
    
    auto validation = ValidateAudioFrame(capture_frame);
    if (validation.isError()) {
        LOG_ERROR("ProcessCaptureStream: {}", validation.message());
        return Result<AudioFrame>::error(
            validation.error(),
            validation.message()
        );
    }
    
#ifdef HAS_WEBRTC_AEC
    // TODO: Process through WebRTC Audio Processing
    // apm_->ProcessStream(...)
    // Return processed frame
#endif
    
    frames_processed_++;
    
    // For now, return a copy of the input frame (no processing)
    AudioFrame output = capture_frame;
    return Result<AudioFrame>::ok(output);
}

Result<void> AecProcessor::SetAecEnabled(bool enable) {
    config_.enable_aec = enable;
    
#ifdef HAS_WEBRTC_AEC
    // TODO: Update WebRTC Audio Processing state
#endif
    
    LOG_INFO("AEC {}", enable ? "enabled" : "disabled");
    return Result<void>::ok();
}

Result<void> AecProcessor::SetAecSuppressionLevel(int32_t level) {
    if (level < 0 || level > 2) {
        return Result<void>::error(
            AudioChatError::InvalidArgument,
            "Invalid suppression level: " + std::to_string(level) + 
            " (must be 0, 1, or 2)"
        );
    }
    
    config_.aec_suppression_level = level;
    
#ifdef HAS_WEBRTC_AEC
    // TODO: Update WebRTC Audio Processing suppression level
#endif
    
    LOG_INFO("AEC suppression level set to {}", level);
    return Result<void>::ok();
}

std::string AecProcessor::GetStatistics() const {
    std::ostringstream ss;
    ss << "AecProcessor Statistics:\n"
       << "  Frames processed: " << frames_processed_ << "\n"
       << "  Sample rate: " << config_.sample_rate_hz << " Hz\n"
       << "  Channels: " << config_.num_channels << "\n"
       << "  AEC enabled: " << (config_.enable_aec ? "yes" : "no") << "\n"
       << "  NS enabled: " << (config_.enable_ns ? "yes" : "no") << "\n"
       << "  AGC enabled: " << (config_.enable_agc ? "yes" : "no");
    return ss.str();
}

} // namespace AudioChat
