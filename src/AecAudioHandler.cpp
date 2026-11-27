#include "AecAudioHandler.h"
#include "AudioHandler.h"
#include "Logger.h"

#include <cstring>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

namespace AudioChat {

Result<void> AecAudioHandler::Config::Validate() const {
    auto validation = aec_config.Validate();
    if (validation.isError()) {
        return validation;
    }
    if (!enable_aec) {
        return Result<void>::ok();
    }
    if (!aec_config.enable_aec) {
        return Result<void>::error(AudioChatError::InvalidCodecConfig,
                                   "AEC pipeline is enabled but processor AEC is disabled");
    }

    validation = buffer_config.Validate();
    if (validation.isError()) {
        return validation;
    }
    if (aec_config.sample_rate_hz != buffer_config.sample_rate_hz) {
        return Result<void>::error(AudioChatError::InvalidCodecConfig,
                                   "AEC and buffer sample rates must match");
    }
    if (aec_config.num_channels != buffer_config.num_channels) {
        return Result<void>::error(AudioChatError::InvalidCodecConfig,
                                   "AEC and buffer channel counts must match");
    }
    if (aec_config.frame_duration_ms != buffer_config.frame_duration_ms) {
        return Result<void>::error(AudioChatError::InvalidCodecConfig,
                                   "AEC and buffer frame durations must match");
    }
    return Result<void>::ok();
}

Result<std::unique_ptr<AecAudioHandler>> AecAudioHandler::Create(
    std::unique_ptr<AudioHandler> audio_handler, const Config& config) {
    if (!audio_handler) {
        return Result<std::unique_ptr<AecAudioHandler>>::error(
            AudioChatError::InvalidArgument, "AudioHandler is null");
    }

    const auto validation = config.Validate();
    if (validation.isError()) {
        return Result<std::unique_ptr<AecAudioHandler>>::error(
            validation.error(), validation.message());
    }

    auto handler = std::unique_ptr<AecAudioHandler>(
        new AecAudioHandler(std::move(audio_handler), config));
    const auto initialized = handler->Initialize();
    if (initialized.isError()) {
        return Result<std::unique_ptr<AecAudioHandler>>::error(
            initialized.error(), initialized.message());
    }
    return Result<std::unique_ptr<AecAudioHandler>>::ok(std::move(handler));
}

AecAudioHandler::AecAudioHandler(std::unique_ptr<AudioHandler> audio_handler,
                                 const Config& config)
    : config_(config), audio_handler_(std::move(audio_handler)) {
    LOG_INFO("AecAudioHandler created: AEC {}", config_.enable_aec ? "enabled" : "disabled");
}

AecAudioHandler::~AecAudioHandler() {
    LOG_INFO("AecAudioHandler destroyed");
}

Result<std::unique_ptr<AecAudioHandler>> AecAudioHandler::CreateHeadless(const Config& config) {
    const auto validation = config.Validate();
    if (validation.isError()) {
        return Result<std::unique_ptr<AecAudioHandler>>::error(
            validation.error(), validation.message());
    }

    auto handler = std::unique_ptr<AecAudioHandler>(new AecAudioHandler(nullptr, config));
    const auto initialized = handler->Initialize();
    if (initialized.isError()) {
        return Result<std::unique_ptr<AecAudioHandler>>::error(
            initialized.error(), initialized.message());
    }
    return Result<std::unique_ptr<AecAudioHandler>>::ok(std::move(handler));
}

Result<void> AecAudioHandler::Initialize() {
    if (!config_.enable_aec) {
        is_initialized_ = true;
        return Result<void>::ok();
    }

    auto processor = AecProcessor::Create(config_.aec_config);
    if (processor.isError()) {
        return Result<void>::error(processor.error(), processor.message());
    }
    aec_processor_ = std::move(processor.value());
    if (!aec_processor_->IsBackendActive()) {
        if (!config_.allow_passthrough_on_error) {
            return Result<void>::error(
                AudioChatError::AecInitFailed,
                "AEC was requested but no real Audio Processing backend is active");
        }
        LOG_WARN("AEC backend unavailable; explicit passthrough mode selected");
        is_initialized_ = true;
        return Result<void>::ok();
    }

    auto buffer = ReferenceSignalBuffer::Create(config_.buffer_config);
    if (buffer.isError()) {
        return Result<void>::error(buffer.error(), buffer.message());
    }
    reference_buffer_ = std::move(buffer.value());

    auto primed = PrimeReferenceBuffer();
    if (primed.isError()) {
        return primed;
    }

    is_initialized_ = true;
    aec_enabled_.store(true);
    LOG_INFO("AEC pipeline initialized");
    return Result<void>::ok();
}

Result<void> AecAudioHandler::PrimeReferenceBuffer() {
    if (!reference_buffer_) {
        return Result<void>::error(AudioChatError::NotInitialized,
                                   "Reference signal buffer not initialized");
    }
    const int32_t priming_samples = config_.aec_config.sample_rate_hz *
        (config_.buffer_config.delay_ms + config_.aec_config.frame_duration_ms) / 1000;
    std::vector<int16_t> silence(
        static_cast<size_t>(priming_samples) * config_.aec_config.num_channels, 0);
    return reference_buffer_->PushReferenceSignal(silence.data(), priming_samples);
}

Result<void> AecAudioHandler::EnableAec() {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    if (!config_.enable_aec || !aec_processor_ || !reference_buffer_) {
        return Result<void>::error(AudioChatError::NotInitialized,
                                   "AEC was not configured at initialization");
    }
    if (aec_enabled_.load()) {
        return Result<void>::ok();
    }

    reference_buffer_->Clear();
    auto reset = aec_processor_->Reset();
    if (reset.isError()) {
        return reset;
    }
    auto primed = PrimeReferenceBuffer();
    if (primed.isError()) {
        return primed;
    }
    aec_enabled_.store(true);
    return Result<void>::ok();
}

Result<void> AecAudioHandler::DisableAec() {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    if (!aec_enabled_.exchange(false)) {
        return Result<void>::ok();
    }
    if (reference_buffer_) {
        reference_buffer_->Clear();
    }
    return Result<void>::ok();
}

Result<void> AecAudioHandler::ProcessReferenceSignal(const int16_t* data,
                                                     int32_t num_samples) {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    if (!aec_enabled_.load()) {
        return Result<void>::ok();
    }
    if (!reference_buffer_) {
        return Result<void>::error(AudioChatError::NotInitialized,
                                   "Reference signal buffer not initialized");
    }
    if (!data || num_samples <= 0 ||
        num_samples > std::numeric_limits<int32_t>::max() / config_.aec_config.num_channels) {
        return Result<void>::error(AudioChatError::InvalidArgument,
                                   "Invalid reference signal");
    }

    auto result = reference_buffer_->PushReferenceSignal(data, num_samples);
    if (result.isOk()) {
        ++reference_frames_processed_;
    } else {
        ++aec_failures_;
    }
    return result;
}

Result<void> AecAudioHandler::ProcessCaptureSignal(const int16_t* input,
                                                   int32_t num_samples,
                                                   int16_t* output) {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    if (!input || !output || num_samples <= 0 ||
        num_samples > std::numeric_limits<int32_t>::max() / config_.aec_config.num_channels) {
        return Result<void>::error(AudioChatError::InvalidArgument,
                                   "Invalid capture signal parameters");
    }

    const size_t sample_count = static_cast<size_t>(num_samples) *
                                static_cast<size_t>(config_.aec_config.num_channels);
    const auto passthrough = [&]() {
        std::memcpy(output, input, sample_count * sizeof(int16_t));
        return Result<void>::ok();
    };
    const auto failure = [&](AudioChatError error, const std::string& message) {
        ++aec_failures_;
        if (config_.allow_passthrough_on_error) {
            if (aec_processor_ && reference_buffer_) {
                reference_buffer_->Clear();
                (void)aec_processor_->Reset();
                (void)PrimeReferenceBuffer();
            }
            LOG_WARN("AEC failed, explicit passthrough mode active: {}", message);
            return passthrough();
        }
        return Result<void>::error(error, message);
    };

    if (!aec_enabled_.load() || !aec_processor_ || !reference_buffer_) {
        return passthrough();
    }

    auto delayed = reference_buffer_->GetDelayedReference(num_samples);
    if (delayed.isError()) {
        return failure(delayed.error(), delayed.message());
    }

    AudioFrame reference_frame;
    reference_frame.timestamp_ms = delayed.value().timestamp_ms;
    reference_frame.data = std::move(delayed.value().data);
    reference_frame.sample_count = num_samples;
    reference_frame.channels = config_.aec_config.num_channels;

    auto reference_error = aec_processor_->ProcessReferenceStream(reference_frame);
    if (reference_error.isError()) {
        return failure(reference_error.error(), reference_error.message());
    }

    AudioFrame capture_frame;
    capture_frame.data.resize(sample_count);
    std::memcpy(capture_frame.data.data(), input, sample_count * sizeof(int16_t));
    capture_frame.sample_count = num_samples;
    capture_frame.channels = config_.aec_config.num_channels;

    auto captured = aec_processor_->ProcessCaptureStream(capture_frame);
    if (captured.isError()) {
        return failure(captured.error(), captured.message());
    }

    AudioFrame processed = captured.value();
    if (processed.data.size() != sample_count) {
        return failure(AudioChatError::AecProcessFailed,
                       "AEC returned an unexpected frame size");
    }
    std::memcpy(output, processed.data.data(), sample_count * sizeof(int16_t));
    ++capture_frames_processed_;
    return Result<void>::ok();
}

Result<void> AecAudioHandler::ReconfigureAec(const AecConfig& aec_config) {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    if (!aec_processor_ || !reference_buffer_) {
        return Result<void>::error(AudioChatError::NotInitialized,
                                   "AEC pipeline not initialized");
    }
    auto validation = aec_config.Validate();
    if (validation.isError()) {
        return validation;
    }
    if (!aec_config.enable_aec) {
        return Result<void>::error(AudioChatError::InvalidCodecConfig,
                                   "Cannot disable processor AEC while the pipeline is enabled");
    }
    if (aec_config.sample_rate_hz != config_.aec_config.sample_rate_hz ||
        aec_config.num_channels != config_.aec_config.num_channels ||
        aec_config.frame_duration_ms != config_.aec_config.frame_duration_ms) {
        return Result<void>::error(
            AudioChatError::InvalidCodecConfig,
            "Runtime AEC format changes require pipeline reinitialization");
    }

    auto processor = AecProcessor::Create(aec_config);
    if (processor.isError()) {
        return Result<void>::error(processor.error(), processor.message());
    }
    if (!processor.value()->IsBackendActive() && !config_.allow_passthrough_on_error) {
        return Result<void>::error(AudioChatError::AecInitFailed,
                                   "Reconfigured AEC backend is not active");
    }
    auto new_buffer = ReferenceSignalBuffer::Create(config_.buffer_config);
    if (new_buffer.isError()) {
        return Result<void>::error(new_buffer.error(), new_buffer.message());
    }
    const int32_t priming_samples = config_.aec_config.sample_rate_hz *
        (config_.buffer_config.delay_ms + config_.aec_config.frame_duration_ms) / 1000;
    std::vector<int16_t> silence(
        static_cast<size_t>(priming_samples) * config_.aec_config.num_channels, 0);
    auto primed = new_buffer.value()->PushReferenceSignal(silence.data(), priming_samples);
    if (primed.isError()) {
        return primed;
    }

    aec_processor_ = std::move(processor.value());
    reference_buffer_ = std::move(new_buffer.value());
    config_.aec_config = aec_config;
    return Result<void>::ok();
}

AecAudioHandler::Config AecAudioHandler::GetConfig() const {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    return config_;
}

std::string AecAudioHandler::GetAecStatistics() const {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    return aec_processor_ ? aec_processor_->GetStatistics()
                          : "AEC processor not initialized";
}

std::string AecAudioHandler::GetBufferStatistics() const {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    return reference_buffer_ ? reference_buffer_->GetStatistics()
                             : "Reference signal buffer not initialized";
}

Result<void> AecAudioHandler::ResetAec() {
    std::lock_guard<std::mutex> lock(pipeline_mutex_);
    if (!aec_processor_ || !reference_buffer_) {
        return Result<void>::error(AudioChatError::NotInitialized,
                                   "AEC pipeline not initialized");
    }
    reference_buffer_->Clear();
    auto reset = aec_processor_->Reset();
    if (reset.isError()) {
        return reset;
    }
    auto primed = PrimeReferenceBuffer();
    if (primed.isError()) {
        return primed;
    }
    reference_frames_processed_ = 0;
    capture_frames_processed_ = 0;
    aec_failures_ = 0;
    return Result<void>::ok();
}

} // namespace AudioChat
