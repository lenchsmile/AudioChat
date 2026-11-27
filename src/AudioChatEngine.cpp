#include "AudioChatEngine.h"

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
        audio_ = std::make_unique<AudioHandler>(
            config_.sample_rate, config_.sample_rate, samples, samples,
            config_.input_device.empty() ? "auto" : config_.input_device.c_str(),
            config_.output_device.empty() ? "auto" : config_.output_device.c_str());

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
            config_.local_port, config_.remote_port);
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
        audio_->stop();
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
        audio_->stop();
    }
    work_cv_.notify_all();
    joinThreads();
    return Result<void>::ok();
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

void AudioChatEngine::requestStop() {
    running_ = false;
    if (network_) {
        network_->stop();
    }
    if (audio_) {
        audio_->stop();
    }
    work_cv_.notify_all();
}

void AudioChatEngine::sendLoop() {
    const int samples = codec_->getFramesPerBuffer();
    std::vector<int16_t> capture(static_cast<size_t>(samples));
    Packet packet;
    packet.sample_rate = static_cast<uint16_t>(config_.sample_rate);
    packet.channels = 1;

    while (running_.load()) {
        try {
            if (audio_->readCaptureFrame(capture.data(), capture.size())) {
                codec_->encode(capture.data(), packet.payload.data(),
                               static_cast<int32_t>(packet.payload.size()), packet.data_size);
                if (packet.data_size > 0) {
                    network_->send(packet);
                }
                continue;
            }
        } catch (const std::exception& error) {
            LOG_ERROR("Send loop error: {}", error.what());
            requestStop();
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
            requestStop();
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

    while (running_.load()) {
        try {
            int32_t encoded_size = 0;
            if (!network_->getJitterBufferPacket(encoded.data(), encoded.size(), encoded_size)) {
                break;
            }
            if (encoded_size <= 0) {
                continue;
            }

            int32_t decoded_size = 0;
            codec_->decode(encoded.data(), encoded_size, decoded.data(), samples, decoded_size);
            if (decoded_size > 0) {
                audio_->play(decoded.data(), decoded_size);
            }
        } catch (const std::exception& error) {
            LOG_ERROR("Playback loop error: {}", error.what());
            requestStop();
            break;
        }
    }
}

void AudioChatEngine::cleanupResources() {
    if (audio_) {
        audio_->stop();
        audio_.reset();
    }
    codec_.reset();
    if (network_) {
        network_->stop();
        network_.reset();
    }
}
