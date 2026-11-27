#include "AecProcessor.h"
#include "Logger.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

#ifdef HAS_WEBRTC_AEC
#include "api/audio/builtin_audio_processing_builder.h"
#include "api/audio/echo_canceller3_config.h"
#include "api/environment/environment_factory.h"
#endif

namespace AudioChat {

struct AecProcessor::Impl {
    std::mutex mutex;
    bool backend_active = false;

#ifdef HAS_WEBRTC_AEC
    webrtc::Environment environment = webrtc::CreateEnvironment();
    webrtc::scoped_refptr<webrtc::AudioProcessing> apm;
    webrtc::StreamConfig stream_config;
    std::vector<int16_t> reverse_scratch;
    std::vector<int16_t> pending_reverse_samples;
#endif
};

namespace {

#ifdef HAS_WEBRTC_AEC
webrtc::EchoCanceller3Config makeEchoConfig() {
    webrtc::EchoCanceller3Config config;
    // Start from WebRTC's validated defaults. Suppression-level tuning must be
    // introduced only with measured echo and double-talk evidence.
    webrtc::EchoCanceller3Config::Validate(&config);
    return config;
}

webrtc::AudioProcessing::Config makeProcessingConfig(const AecConfig& source) {
    webrtc::AudioProcessing::Config config;
    config.echo_canceller.enabled = source.enable_aec;
    config.echo_canceller.enforce_high_pass_filtering = true;
    config.high_pass_filter.enabled = true;
    config.noise_suppression.enabled = source.enable_ns;
    config.noise_suppression.level =
        static_cast<webrtc::AudioProcessing::Config::NoiseSuppression::Level>(source.ns_level);
    config.gain_controller1.enabled = source.enable_agc;
    config.gain_controller1.mode =
        webrtc::AudioProcessing::Config::GainController1::kAdaptiveDigital;
    config.gain_controller1.analog_gain_controller.enabled = false;
    config.gain_controller1.target_level_dbfs = source.enable_agc_target_loudness ? 3 : 0;
    config.gain_controller1.enable_limiter = true;
    return config;
}
#endif

} // namespace

Result<void> AecConfig::Validate() const {
    if (sample_rate_hz != 8000 && sample_rate_hz != 16000 &&
        sample_rate_hz != 32000 && sample_rate_hz != 48000) {
        return Result<void>::error(
            AudioChatError::InvalidCodecConfig,
            "Invalid sample rate: " + std::to_string(sample_rate_hz) +
            " Hz (supported: 8000, 16000, 32000, 48000)");
    }
    if (num_channels < 1 || num_channels > 2) {
        return Result<void>::error(AudioChatError::InvalidCodecConfig,
                                   "Invalid number of channels: " + std::to_string(num_channels));
    }
    if (frame_duration_ms != 10 && frame_duration_ms != 20) {
        return Result<void>::error(
            AudioChatError::InvalidCodecConfig,
            "Invalid frame duration: " + std::to_string(frame_duration_ms) +
            " ms (must be 10 or 20)");
    }
    if (aec_suppression_level < 0 || aec_suppression_level > 2) {
        return Result<void>::error(AudioChatError::AecInitFailed,
                                   "Invalid AEC suppression level");
    }
    if (stream_delay_ms < 0 || stream_delay_ms > 500) {
        return Result<void>::error(AudioChatError::AecInitFailed,
                                   "Invalid AEC stream delay");
    }
    if (ns_level < 0 || ns_level > 2) {
        return Result<void>::error(AudioChatError::AecInitFailed,
                                   "Invalid NS level");
    }
    return Result<void>::ok();
}

Result<std::unique_ptr<AecProcessor>> AecProcessor::Create(const AecConfig& config) {
    const auto validation = config.Validate();
    if (validation.isError()) {
        return Result<std::unique_ptr<AecProcessor>>::error(
            validation.error(), validation.message());
    }

    auto processor = std::unique_ptr<AecProcessor>(new AecProcessor(config));
    const auto initialized = processor->Initialize();
    if (initialized.isError()) {
        return Result<std::unique_ptr<AecProcessor>>::error(
            initialized.error(), initialized.message());
    }
    return Result<std::unique_ptr<AecProcessor>>::ok(std::move(processor));
}

AecProcessor::AecProcessor(const AecConfig& config)
    : config_(config), impl_(std::make_unique<Impl>()) {
    LOG_INFO("AecProcessor created: {}Hz, {} channels, {}ms",
             config.sample_rate_hz, config.num_channels, config.frame_duration_ms);
}

AecProcessor::~AecProcessor() = default;

Result<void> AecProcessor::Initialize() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return ResetLocked();
}

Result<void> AecProcessor::ResetLocked() {
    const auto validation = config_.Validate();
    if (validation.isError()) {
        return validation;
    }

    if (!config_.enable_aec && !config_.enable_ns && !config_.enable_agc) {
        impl_->backend_active = false;
#ifdef HAS_WEBRTC_AEC
        impl_->apm = nullptr;
        impl_->pending_reverse_samples.clear();
#endif
        is_initialized_ = true;
        LOG_INFO("Audio processing explicitly disabled");
        return Result<void>::ok();
    }

#ifndef HAS_WEBRTC_AEC
    // Developer-only no-AEC build. Release builds use REQUIRE_AEC=ON and fail
    // configuration before reaching this path.
    impl_->backend_active = false;
    is_initialized_ = true;
    LOG_WARN("No WebRTC backend compiled; audio processing is a passthrough");
    return Result<void>::ok();
#else
    try {
        webrtc::AudioProcessing::Config processing_config = makeProcessingConfig(config_);
        webrtc::BuiltinAudioProcessingBuilder builder;
        builder.SetConfig(processing_config);
        std::optional<webrtc::EchoCanceller3Config> multichannel_echo;
        if (config_.num_channels > 1) {
            multichannel_echo = webrtc::EchoCanceller3Config::CreateDefaultMultichannelConfig();
        }
        builder.SetEchoCancellerConfig(makeEchoConfig(), multichannel_echo);

        auto new_apm = builder.Build(impl_->environment);
        if (!new_apm) {
            return Result<void>::error(AudioChatError::AecInitFailed,
                                       "WebRTC AudioProcessing builder returned null");
        }

        webrtc::StreamConfig new_stream_config(
            config_.sample_rate_hz, static_cast<size_t>(config_.num_channels));
        std::vector<int16_t> new_scratch(
            static_cast<size_t>(config_.sample_rate_hz / 100) * config_.num_channels);

        int error = new_apm->Initialize();
        if (error == webrtc::AudioProcessing::kNoError && config_.enable_aec &&
            config_.stream_delay_ms > 0) {
            error = new_apm->set_stream_delay_ms(config_.stream_delay_ms);
        }
        if (error != webrtc::AudioProcessing::kNoError) {
            return Result<void>::error(
                AudioChatError::AecInitFailed,
                "WebRTC AudioProcessing initialization failed: " + std::to_string(error));
        }

        impl_->apm = std::move(new_apm);
        impl_->stream_config = std::move(new_stream_config);
        impl_->reverse_scratch = std::move(new_scratch);
        impl_->pending_reverse_samples.clear();
        impl_->backend_active = true;
        is_initialized_ = true;
        frames_processed_ = 0;
        LOG_INFO("WebRTC AudioProcessing initialized");
        return Result<void>::ok();
    } catch (const std::exception& error) {
        return Result<void>::error(AudioChatError::AecInitFailed,
                                   std::string("WebRTC initialization failed: ") + error.what());
    }
#endif
}

Result<void> AecProcessor::ValidateAudioFrame(const AudioFrame& frame) const {
    if (frame.data.empty()) {
        return Result<void>::error(AudioChatError::AecProcessFailed,
                                   "Audio frame data is empty");
    }
    if (frame.sample_count <= 0) {
        return Result<void>::error(AudioChatError::AecProcessFailed,
                                   "Invalid sample count");
    }
    if (frame.channels != config_.num_channels) {
        return Result<void>::error(AudioChatError::AecProcessFailed,
                                   "Frame channel count does not match AEC configuration");
    }
    if (frame.sample_count != config_.sample_rate_hz * config_.frame_duration_ms / 1000) {
        return Result<void>::error(AudioChatError::AecProcessFailed,
                                   "Frame duration does not match AEC configuration");
    }

    const size_t expected = static_cast<size_t>(frame.sample_count) * frame.channels;
    if (frame.data.size() != expected) {
        return Result<void>::error(AudioChatError::AecProcessFailed,
                                   "Frame size mismatch");
    }
    return Result<void>::ok();
}

Result<void> AecProcessor::ProcessReferenceStream(const AudioFrame& reference_frame) {
    if (!is_initialized_) {
        return Result<void>::error(AudioChatError::AecProcessFailed,
                                   "AEC processor not initialized");
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto validation = ValidateAudioFrame(reference_frame);
    if (validation.isError()) {
        return validation;
    }

#ifndef HAS_WEBRTC_AEC
    ++frames_processed_;
    return Result<void>::ok();
#else
    if (!config_.enable_aec) {
        ++frames_processed_;
        return Result<void>::ok();
    }
    if (!impl_->backend_active || !impl_->apm) {
        return Result<void>::error(AudioChatError::AecProcessFailed,
                                   "WebRTC backend is not active");
    }
    if (!impl_->pending_reverse_samples.empty()) {
        return Result<void>::error(AudioChatError::AecProcessFailed,
                                   "A reference frame is already pending capture processing");
    }

    // Queue the reference frame. Capture processing consumes reference and
    // capture chunks in an interleaved order, which matters for stateful APM.
    impl_->pending_reverse_samples = reference_frame.data;
    ++frames_processed_;
    return Result<void>::ok();
#endif
}

Result<AudioFrame> AecProcessor::ProcessCaptureStream(const AudioFrame& capture_frame) {
    if (!is_initialized_) {
        return Result<AudioFrame>::error(AudioChatError::AecProcessFailed,
                                         "AEC processor not initialized");
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto validation = ValidateAudioFrame(capture_frame);
    if (validation.isError()) {
        return Result<AudioFrame>::error(validation.error(), validation.message());
    }

    AudioFrame output = capture_frame;
#ifndef HAS_WEBRTC_AEC
    ++frames_processed_;
    return Result<AudioFrame>::ok(std::move(output));
#else
    if (!config_.enable_aec && !config_.enable_ns && !config_.enable_agc) {
        ++frames_processed_;
        return Result<AudioFrame>::ok(std::move(output));
    }
    if (!impl_->backend_active || !impl_->apm) {
        return Result<AudioFrame>::error(AudioChatError::AecProcessFailed,
                                         "WebRTC backend is not active");
    }

    output.data.resize(capture_frame.data.size());
    const int chunk_samples = config_.sample_rate_hz / 100;
    const int chunks = config_.frame_duration_ms / 10;
    const size_t chunk_values = static_cast<size_t>(chunk_samples) * config_.num_channels;

    if (config_.enable_aec && !impl_->pending_reverse_samples.empty() &&
        impl_->pending_reverse_samples.size() != capture_frame.data.size()) {
        return Result<AudioFrame>::error(AudioChatError::AecProcessFailed,
                                         "Capture frame has no matching reference frame");
    }
    const bool has_reference = config_.enable_aec && !impl_->pending_reverse_samples.empty();

    for (int chunk = 0; chunk < chunks; ++chunk) {
        const size_t offset = static_cast<size_t>(chunk) * chunk_values;
        if (has_reference) {
            std::copy_n(impl_->pending_reverse_samples.begin() +
                            static_cast<std::ptrdiff_t>(offset),
                        chunk_values, impl_->reverse_scratch.begin());
            const int reverse_error = impl_->apm->ProcessReverseStream(
                impl_->reverse_scratch.data(), impl_->stream_config,
                impl_->stream_config, impl_->reverse_scratch.data());
            if (reverse_error != webrtc::AudioProcessing::kNoError) {
                impl_->pending_reverse_samples.clear();
                return Result<AudioFrame>::error(
                    AudioChatError::AecProcessFailed,
                    "ProcessReverseStream failed: " + std::to_string(reverse_error));
            }
        }

        const int error = impl_->apm->ProcessStream(
            capture_frame.data.data() + offset, impl_->stream_config,
            impl_->stream_config, output.data.data() + offset);
        if (error != webrtc::AudioProcessing::kNoError) {
            impl_->pending_reverse_samples.clear();
            return Result<AudioFrame>::error(
                AudioChatError::AecProcessFailed,
                "ProcessStream failed: " + std::to_string(error));
        }
    }

    impl_->pending_reverse_samples.clear();
    ++frames_processed_;
    return Result<AudioFrame>::ok(std::move(output));
#endif
}

Result<void> AecProcessor::SetAecEnabled(bool enable) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const bool previous = config_.enable_aec;
    config_.enable_aec = enable;
    auto result = ResetLocked();
    if (result.isError()) {
        config_.enable_aec = previous;
    }
    return result;
}

Result<void> AecProcessor::SetAecSuppressionLevel(int32_t level) {
    if (level < 0 || level > 2) {
        return Result<void>::error(AudioChatError::InvalidArgument,
                                   "Invalid suppression level");
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const int32_t previous = config_.aec_suppression_level;
    config_.aec_suppression_level = level;
    auto result = ResetLocked();
    if (result.isError()) {
        config_.aec_suppression_level = previous;
    }
    return result;
}

Result<void> AecProcessor::Reset() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return ResetLocked();
}

AecConfig AecProcessor::GetConfig() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return config_;
}

bool AecProcessor::IsBackendActive() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->backend_active;
}

std::optional<double> AecProcessor::GetEchoReturnLossEnhancement() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
#ifdef HAS_WEBRTC_AEC
    if (impl_->backend_active && impl_->apm) {
        return impl_->apm->GetStatistics().echo_return_loss_enhancement;
    }
#endif
    return std::nullopt;
}

std::optional<int32_t> AecProcessor::GetAecDelayMs() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
#ifdef HAS_WEBRTC_AEC
    if (impl_->backend_active && impl_->apm) {
        return impl_->apm->GetStatistics().delay_ms;
    }
#endif
    return std::nullopt;
}

std::string AecProcessor::GetStatistics() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::optional<double> erle;
    std::optional<int32_t> delay_ms;
#ifdef HAS_WEBRTC_AEC
    if (impl_->backend_active && impl_->apm) {
        const auto stats = impl_->apm->GetStatistics();
        erle = stats.echo_return_loss_enhancement;
        delay_ms = stats.delay_ms;
    }
#endif
    std::ostringstream stream;
    stream << "AecProcessor Statistics:\n"
           << "  Frames processed: " << frames_processed_ << "\n"
           << "  Sample rate: " << config_.sample_rate_hz << " Hz\n"
           << "  Channels: " << config_.num_channels << "\n"
           << "  AEC enabled: " << (config_.enable_aec ? "yes" : "no") << "\n"
           << "  Backend active: " << (impl_->backend_active ? "yes" : "no") << "\n"
           << "  NS enabled: " << (config_.enable_ns ? "yes" : "no") << "\n"
           << "  AGC enabled: " << (config_.enable_agc ? "yes" : "no");
    if (erle) {
        stream << "\n  WebRTC reported ERLE: " << *erle << " dB";
    }
    if (delay_ms) {
        stream << "\n  WebRTC estimated delay: " << *delay_ms << " ms";
    }
    return stream.str();
}

} // namespace AudioChat
