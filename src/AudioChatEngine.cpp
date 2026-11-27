#include "AudioChatEngine.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <thread>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

AudioChatEngine::AudioChatEngine(const AudioChatConfig& config) : config_(config) {
    LOG_INFO("AudioChatEngine created: local={}:{}, remote={}:{}",
             config_.local_ip, config_.local_port, config_.remote_ip, config_.remote_port);
}

AudioChatEngine::~AudioChatEngine() {
    stop();
    cleanupResources();
}

Result<void> AudioChatEngine::init() {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    if (initialized_.load()) {
        return Result<void>::error(AudioChatError::AlreadyInitialized,
                                   "Engine already initialized");
    }
    if (running_.load()) {
        return Result<void>::error(AudioChatError::InternalError,
                                   "Engine is running during initialization");
    }

    const auto result = initComponents();
    if (result.isError()) {
        cleanupResources();
        return result;
    }

    initialized_ = true;
    LOG_INFO("AudioChatEngine initialized");
    return Result<void>::ok();
}

Result<void> AudioChatEngine::initComponents() {
    if (config_.local_port <= 0 || config_.local_port > 65535 ||
        config_.remote_port <= 0 || config_.remote_port > 65535 ||
        (config_.sample_rate != 8000 && config_.sample_rate != 16000 &&
         config_.sample_rate != 32000 && config_.sample_rate != 48000) ||
        (config_.frame_duration_ms != 10 && config_.frame_duration_ms != 20)) {
        return Result<void>::error(AudioChatError::InvalidNetworkConfig,
                                   "Invalid engine audio or network configuration");
    }

    try {
        const int samples = config_.sample_rate * config_.frame_duration_ms / 1000;
        auto raw_audio = std::make_unique<AudioHandler>(
            config_.sample_rate, config_.sample_rate, samples, samples,
            config_.input_device.empty() ? "auto" : config_.input_device.c_str(),
            config_.output_device.empty() ? "auto" : config_.output_device.c_str(),
            nullptr, false);

        AudioChat::AecConfig aec_config;
        aec_config.sample_rate_hz = config_.sample_rate;
        aec_config.num_channels = 1;
        aec_config.frame_duration_ms = config_.frame_duration_ms;
        aec_config.enable_aec = config_.enable_aec;
        aec_config.aec_suppression_level = config_.aec_suppression_level;
        aec_config.enable_ns = config_.enable_ns;
        aec_config.enable_agc = config_.enable_agc;

        AudioChat::ReferenceSignalBuffer::Config reference_config;
        reference_config.sample_rate_hz = config_.sample_rate;
        reference_config.num_channels = 1;
        reference_config.frame_duration_ms = config_.frame_duration_ms;
        reference_config.delay_ms = config_.reference_delay_ms;
        reference_config.buffer_duration_ms =
            std::max(config_.reference_buffer_ms, config_.reference_delay_ms + 100);

        AudioChat::AecAudioHandler::Config pipeline_config;
        pipeline_config.enable_aec = config_.enable_aec;
        pipeline_config.allow_passthrough_on_error = config_.allow_aec_passthrough;
        pipeline_config.aec_config = aec_config;
        pipeline_config.buffer_config = reference_config;

        auto pipeline = AudioChat::AecAudioHandler::Create(
            std::move(raw_audio), pipeline_config);
        if (pipeline.isError()) {
            return Result<void>::error(pipeline.error(), pipeline.message());
        }
        audio_ = std::move(pipeline.value());

        codec_ = std::make_unique<Codec>(config_.sample_rate, 1, config_.frame_duration_ms);

        int error = opus_encoder_ctl(codec_->getEncoder(), OPUS_SET_BITRATE(config_.bitrate));
        if (error != OPUS_OK) {
            return Result<void>::error(AudioChatError::InvalidCodecConfig,
                                       "Failed to set Opus bitrate");
        }
        error = opus_encoder_ctl(codec_->getEncoder(), OPUS_SET_COMPLEXITY(config_.complexity));
        if (error != OPUS_OK) {
            return Result<void>::error(AudioChatError::InvalidCodecConfig,
                                       "Failed to set Opus complexity");
        }
        error = opus_encoder_ctl(codec_->getEncoder(),
                                 OPUS_SET_INBAND_FEC(config_.enable_fec ? 1 : 0));
        if (error != OPUS_OK) {
            return Result<void>::error(AudioChatError::InvalidCodecConfig,
                                       "Failed to configure Opus FEC");
        }
        error = opus_encoder_ctl(codec_->getEncoder(), OPUS_SET_DTX(config_.enable_dtx ? 1 : 0));
        if (error != OPUS_OK) {
            return Result<void>::error(AudioChatError::InvalidCodecConfig,
                                       "Failed to configure Opus DTX");
        }
        error = opus_encoder_ctl(codec_->getEncoder(),
                                 OPUS_SET_PACKET_LOSS_PERC(config_.packet_loss_perc));
        if (error != OPUS_OK) {
            return Result<void>::error(AudioChatError::InvalidCodecConfig,
                                       "Failed to configure Opus packet loss");
        }

        network_ = std::make_unique<NetworkHandler>(
            config_.local_ip.c_str(), config_.remote_ip.c_str(),
            config_.local_port, config_.remote_port,
            static_cast<uint16_t>(config_.sample_rate), 1,
            static_cast<uint16_t>(config_.frame_duration_ms));
        return Result<void>::ok();
    } catch (const std::exception& error) {
        return Result<void>::error(AudioChatError::InternalError, error.what());
    }
}

Result<void> AudioChatEngine::start() {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    if (!initialized_.load()) {
        return Result<void>::error(AudioChatError::NotInitialized,
                                   "Engine not initialized");
    }
    if (running_.load()) {
        return Result<void>::ok();
    }

    try {
        if (audio_) {
            auto* raw_audio = audio_->GetAudioHandler();
            raw_audio->start();
            raw_audio->discardCaptureFrames();
        }
    } catch (const std::exception& error) {
        cleanupResources();
        initialized_ = false;
        return Result<void>::error(AudioChatError::InternalError,
                                   std::string("Failed to start audio devices: ") + error.what());
    }

    running_ = true;
    const auto result = startThreads();
    if (result.isError()) {
        running_ = false;
        if (network_) {
            network_->stop();
        }
        work_cv_.notify_all();
        joinThreads();
        cleanupResources();
        initialized_ = false;
        return result;
    }
    LOG_INFO("AudioChatEngine started");
    return Result<void>::ok();
}

Result<void> AudioChatEngine::startThreads() {
    try {
        send_thread_ = std::thread(&AudioChatEngine::sendLoop, this);
        recv_thread_ = std::thread(&AudioChatEngine::recvLoop, this);
        play_thread_ = std::thread(&AudioChatEngine::playLoop, this);
        return Result<void>::ok();
    } catch (const std::exception& error) {
        return Result<void>::error(AudioChatError::InternalError,
                                   "Failed to start engine threads: " + std::string(error.what()));
    }
}

Result<void> AudioChatEngine::stop() {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    if (!running_.load() && !send_thread_.joinable() &&
        !recv_thread_.joinable() && !play_thread_.joinable()) {
        initialized_ = false;
        cleanupResources();
        return Result<void>::ok();
    }

    LOG_INFO("AudioChatEngine stopping");
    running_ = false;
    if (network_) {
        network_->stop();
    }
    if (audio_) {
        audio_->GetAudioHandler()->stop();
    }
    work_cv_.notify_all();
    joinThreads();
    cleanupResources();
    initialized_ = false;
    LOG_INFO("AudioChatEngine stopped");
    return Result<void>::ok();
}

Result<void> AudioChatEngine::stopThreads() {
    running_ = false;
    if (network_) {
        network_->stop();
    }
    if (audio_) {
        audio_->GetAudioHandler()->stop();
    }
    work_cv_.notify_all();
    joinThreads();
    return Result<void>::ok();
}

bool AudioChatEngine::isAecActive() {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    return audio_ && audio_->IsAecEnabled();
}

std::string AudioChatEngine::getAecStatistics() {
    std::lock_guard<std::mutex> lock(lifecycle_mutex_);
    return audio_ ? audio_->GetAecStatistics() : "AEC pipeline not initialized";
}

void AudioChatEngine::joinThreads() {
    if (send_thread_.joinable()) {
        send_thread_.join();
    }
    if (recv_thread_.joinable()) {
        recv_thread_.join();
    }
    if (play_thread_.joinable()) {
        play_thread_.join();
    }
}

bool AudioChatEngine::hasFatalError() const {
    std::lock_guard<std::mutex> lock(fatal_error_mutex_);
    return !fatal_error_.empty();
}

std::string AudioChatEngine::getFatalError() const {
    std::lock_guard<std::mutex> lock(fatal_error_mutex_);
    return fatal_error_;
}

void AudioChatEngine::requestStop(const std::string& error) {
    {
        std::lock_guard<std::mutex> lock(fatal_error_mutex_);
        if (fatal_error_.empty()) {
            fatal_error_ = error;
        }
    }
    running_ = false;
    if (network_) {
        network_->stop();
    }
    if (audio_) {
        audio_->GetAudioHandler()->stop();
    }
    work_cv_.notify_all();
}

void AudioChatEngine::sendLoop() {
    const int samples = codec_->getFramesPerBuffer();
    std::vector<int16_t> capture(static_cast<size_t>(samples));
    std::vector<int16_t> processed(static_cast<size_t>(samples));
    Packet packet;
    packet.sample_rate = static_cast<uint16_t>(config_.sample_rate);
    packet.channels = 1;
    packet.frame_duration_ms = static_cast<uint16_t>(config_.frame_duration_ms);
    const auto frame_period = std::chrono::milliseconds(config_.frame_duration_ms);
    auto next_deadline = std::chrono::steady_clock::now();

    while (running_.load()) {
        try {
            if (audio_->GetAudioHandler()->readLatestCaptureFrame(capture.data(), capture.size())) {
                auto capture_result = audio_->ProcessCaptureSignal(
                    capture.data(), samples, processed.data());
                for (int retry = 0;
                     capture_result.isError() &&
                     capture_result.error() == AudioChatError::BufferUnderflow &&
                     retry < config_.frame_duration_ms &&
                     running_.load();
                     ++retry) {
                    std::unique_lock<std::mutex> lock(work_mutex_);
                    work_cv_.wait_for(lock, 1ms, [this] { return !running_.load(); });
                    capture_result = audio_->ProcessCaptureSignal(
                        capture.data(), samples, processed.data());
                }
                if (capture_result.isError()) {
                    throw std::runtime_error("AEC capture processing failed: " +
                                             capture_result.message());
                }
                codec_->encode(processed.data(), packet.payload.data(),
                               static_cast<int32_t>(packet.payload.size()), packet.data_size);
                if (packet.data_size > 0) {
                    network_->send(packet);
                }
                next_deadline += frame_period;
                const auto now = std::chrono::steady_clock::now();
                if (next_deadline <= now) {
                    next_deadline = now + frame_period;
                }
                std::unique_lock<std::mutex> lock(work_mutex_);
                work_cv_.wait_until(lock, next_deadline,
                                    [this] { return !running_.load(); });
                continue;
            }
        } catch (const std::exception& error) {
            LOG_ERROR("Send loop error: {}", error.what());
            requestStop("Send loop error: " + std::string(error.what()));
            break;
        }

        std::unique_lock<std::mutex> lock(work_mutex_);
        work_cv_.wait_for(lock, 2ms, [this] { return !running_.load(); });
    }
}

void AudioChatEngine::recvLoop() {
    while (running_.load()) {
        try {
            if (network_->receive()) {
                continue;
            }
        } catch (const std::exception& error) {
            LOG_ERROR("Receive loop error: {}", error.what());
            requestStop("Receive loop error: " + std::string(error.what()));
            break;
        }

        std::unique_lock<std::mutex> lock(work_mutex_);
        work_cv_.wait_for(lock, 5ms, [this] { return !running_.load(); });
    }
}

void AudioChatEngine::playLoop() {
    const int samples = codec_->getFramesPerBuffer();
    std::array<uint8_t, kMaxAudioPacketPayload> encoded{};
    std::vector<int16_t> decoded(static_cast<size_t>(samples));
    const auto frame_period = std::chrono::milliseconds(config_.frame_duration_ms);
    auto next_deadline = std::chrono::steady_clock::now() + frame_period;

    while (running_.load()) {
        try {
            const auto now = std::chrono::steady_clock::now();
            int timeout_ms = 0;
            if (next_deadline > now) {
                timeout_ms = std::max(
                    1, static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                           next_deadline - now).count()));
            }

            int32_t encoded_size = 0;
            const auto packet_result = network_->getJitterBufferPacketWithTimeout(
                encoded.data(), encoded.size(), encoded_size, timeout_ms);
            if (packet_result == JitterBuffer::Result::Stopped) {
                break;
            }

            int32_t decoded_size = 0;
            if (packet_result == JitterBuffer::Result::Timeout ||
                (packet_result == JitterBuffer::Result::Packet && encoded_size == 0)) {
                std::fill(decoded.begin(), decoded.end(), 0);
                decoded_size = samples;
            } else if (packet_result == JitterBuffer::Result::Packet) {
                codec_->decode(encoded.data(), encoded_size, decoded.data(), samples, decoded_size);
            } else {
                throw std::runtime_error("Invalid jitter buffer destination");
            }

            if (decoded_size != samples) {
                LOG_WARN("Decoded frame size {} does not match local frame size {}; resynchronizing",
                         decoded_size, samples);
                if (config_.enable_aec && audio_->IsAecEnabled()) {
                    auto reset = audio_->ResetAec();
                    if (reset.isError()) {
                        throw std::runtime_error("AEC resynchronization failed: " + reset.message());
                    }
                }
                std::fill(decoded.begin(), decoded.end(), 0);
                decoded_size = samples;
            }

            if (decoded_size > 0) {
                auto reference_result = audio_->ProcessReferenceSignal(
                    decoded.data(), decoded_size);
                if (reference_result.isError()) {
                    throw std::runtime_error("AEC reference processing failed: " +
                                             reference_result.message());
                }
                audio_->GetAudioHandler()->play(decoded.data(), decoded_size);
            }

            next_deadline += frame_period;
            const auto after_playback = std::chrono::steady_clock::now();
            if (next_deadline <= after_playback) {
                next_deadline = after_playback + frame_period;
            }
        } catch (const std::exception& error) {
            LOG_ERROR("Playback loop error: {}", error.what());
            requestStop("Playback loop error: " + std::string(error.what()));
            break;
        }
    }
}

void AudioChatEngine::cleanupResources() {
    if (audio_) {
        audio_->GetAudioHandler()->stop();
        audio_.reset();
    }
    codec_.reset();
    if (network_) {
        network_->stop();
        network_.reset();
    }
}
