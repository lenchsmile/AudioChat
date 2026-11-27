#include "AudioChatEngine.h"
#include <windows.h>
#include <iostream>

AudioChatEngine::AudioChatEngine(const AudioChatConfig& config)
    : config_(config) {
    LOG_INFO("AudioChatEngine created with config: local={}:{}, remote={}:{}",
             config_.local_ip, config_.local_port,
             config_.remote_ip, config_.remote_port);
}

AudioChatEngine::~AudioChatEngine() {
    LOG_INFO("AudioChatEngine destructor called");
    if (running_.load()) {
        stop();
    }
    cleanupResources();
}

Result<void> AudioChatEngine::init() {
    if (initialized_.load()) {
        LOG_WARN("Engine already initialized");
        return Result<void>::error(AudioChatError::AlreadyInitialized);
    }

    LOG_INFO("Initializing AudioChat engine");

    auto result = initComponents();
    if (result.isError()) {
        LOG_ERROR("Failed to initialize components: {}", result.message());
        return result;
    }

    initialized_.store(true);
    LOG_INFO("AudioChat engine initialized successfully");
    return Result<void>::ok();
}

Result<void> AudioChatEngine::initComponents() {
    // Initialize audio handler
    try {
        HANDLE capture_ready_event = CreateEventA(NULL, FALSE, FALSE, NULL);
        if (!capture_ready_event) {
            LOG_ERROR("Failed to create capture ready event");
            return Result<void>::error(AudioChatError::AudioCallbackError,
                                      "Failed to create Windows event");
        }

        audio_ = std::make_unique<AudioHandler>(
            config_.sample_rate,
            config_.sample_rate,
            config_.sample_rate * config_.frame_duration_ms / 1000,
            config_.sample_rate * config_.frame_duration_ms / 1000,
            config_.input_device.empty() ? "" : config_.input_device.c_str(),
            config_.output_device.empty() ? "" : config_.output_device.c_str(),
            capture_ready_event
        );
        LOG_INFO("AudioHandler initialized");
    } catch (const std::exception& e) {
        LOG_ERROR("AudioHandler initialization failed: {}", e.what());
        return Result<void>::error(AudioChatError::AudioStreamOpenFailed, e.what());
    }

    // Initialize codec
    try {
        codec_ = std::make_unique<Codec>(config_.sample_rate, 1);
        
        // Apply codec configuration
        int err = opus_encoder_ctl(codec_->getEncoder(), 
                                   OPUS_SET_BITRATE(config_.bitrate));
        if (err != OPUS_OK) {
            LOG_ERROR("Failed to set Opus bitrate");
            return Result<void>::error(AudioChatError::InvalidCodecConfig);
        }

        err = opus_encoder_ctl(codec_->getEncoder(), 
                               OPUS_SET_COMPLEXITY(config_.complexity));
        if (err != OPUS_OK) {
            LOG_ERROR("Failed to set Opus complexity");
            return Result<void>::error(AudioChatError::InvalidCodecConfig);
        }

        if (config_.enable_fec) {
            err = opus_encoder_ctl(codec_->getEncoder(), OPUS_SET_INBAND_FEC(1));
            if (err != OPUS_OK) {
                LOG_WARN("Failed to enable Opus FEC");
            } else {
                LOG_INFO("Opus FEC enabled");
            }
        }

        if (config_.enable_dtx) {
            err = opus_encoder_ctl(codec_->getEncoder(), OPUS_SET_DTX(1));
            if (err != OPUS_OK) {
                LOG_WARN("Failed to enable Opus DTX");
            } else {
                LOG_INFO("Opus DTX enabled");
            }
        }

        err = opus_encoder_ctl(codec_->getEncoder(), 
                               OPUS_SET_PACKET_LOSS_PERC(config_.packet_loss_perc));
        if (err != OPUS_OK) {
            LOG_WARN("Failed to set packet loss percentage");
        }

        LOG_INFO("Codec initialized with bitrate={}, complexity={}", 
                 config_.bitrate, config_.complexity);
    } catch (const std::exception& e) {
        LOG_ERROR("Codec initialization failed: {}", e.what());
        return Result<void>::error(AudioChatError::CodecInitFailed, e.what());
    }

    // Initialize network handler
    try {
        network_ = std::make_unique<NetworkHandler>(
            config_.local_ip.c_str(),
            config_.remote_ip.c_str(),
            config_.local_port,
            config_.remote_port
        );
        LOG_INFO("NetworkHandler initialized");
    } catch (const std::exception& e) {
        LOG_ERROR("NetworkHandler initialization failed: {}", e.what());
        return Result<void>::error(AudioChatError::NetworkBindFailed, e.what());
    }

    return Result<void>::ok();
}

Result<void> AudioChatEngine::start() {
    if (!initialized_.load()) {
        LOG_ERROR("Engine not initialized");
        return Result<void>::error(AudioChatError::NotInitialized);
    }

    if (running_.load()) {
        LOG_WARN("Engine already running");
        return Result<void>::ok();
    }

    LOG_INFO("Starting AudioChat engine");

    running_.store(true);

    auto result = startThreads();
    if (result.isError()) {
        running_.store(false);
        LOG_ERROR("Failed to start threads: {}", result.message());
        return result;
    }

    LOG_INFO("AudioChat engine started successfully");
    return Result<void>::ok();
}

Result<void> AudioChatEngine::startThreads() {
    try {
        send_thread_ = std::thread(&AudioChatEngine::sendLoop, this);
        recv_thread_ = std::thread(&AudioChatEngine::recvLoop, this);
        play_thread_ = std::thread(&AudioChatEngine::playLoop, this);

        LOG_INFO("All threads started");
        return Result<void>::ok();
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to start threads: {}", e.what());
        return Result<void>::error(AudioChatError::InternalError, e.what());
    }
}

Result<void> AudioChatEngine::stop() {
    if (!running_.load()) {
        LOG_INFO("Engine not running, nothing to stop");
        return Result<void>::ok();
    }

    LOG_INFO("Stopping AudioChat engine");

    return stopThreads();
}

Result<void> AudioChatEngine::stopThreads() {
    running_.store(false);

    // Signal all threads to wake up and check running flag
    shutdown_cv_.notify_all();

    // Join all threads with timeout
    const int timeout_ms = 5000;
    auto start_time = std::chrono::steady_clock::now();

    LOG_INFO("Waiting for send thread to stop...");
    if (send_thread_.joinable()) {
        send_thread_.join();
    }

    LOG_INFO("Waiting for recv thread to stop...");
    if (recv_thread_.joinable()) {
        recv_thread_.join();
    }

    LOG_INFO("Waiting for play thread to stop...");
    if (play_thread_.joinable()) {
        play_thread_.join();
    }

    LOG_INFO("All threads stopped");
    return Result<void>::ok();
}

void AudioChatEngine::sendLoop() {
    LOG_INFO("Send thread started");
    
    int input_frames_per_buffer = config_.sample_rate * config_.frame_duration_ms / 1000;
    int max_encode_size = input_frames_per_buffer * 2;

    // Allocate buffers
    auto packet_ptr = reinterpret_cast<PPacket>(malloc(sizeof(Packet) + max_encode_size * sizeof(uint8_t)));
    auto capture_buffer_ptr = reinterpret_cast<int16_t*>(malloc(input_frames_per_buffer * sizeof(int16_t)));

    if (!packet_ptr || !capture_buffer_ptr) {
        LOG_ERROR("Failed to allocate buffers in send thread");
        free(packet_ptr);
        free(capture_buffer_ptr);
        return;
    }

    uint8_t* encode_buffer_ptr = packet_ptr->payload;

    while (running_.load()) {
        try {
            // Wait for capture ready event (this should be refactored to condition_variable)
            // For now, using a small sleep to prevent busy waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.frame_duration_ms));

            if (!running_.load()) break;

            // Swap capture buffer
            audio_->swapCaptureBuffer(capture_buffer_ptr);

            // Encode
            codec_->encode(capture_buffer_ptr, encode_buffer_ptr, max_encode_size, packet_ptr->data_size);

            // Send
            network_->send(packet_ptr);

            LOG_TRACE("Sent encoded frame: size={}", packet_ptr->data_size);
        } catch (const std::exception& e) {
            LOG_ERROR("Error in send thread: {}", e.what());
        }
    }

    free(packet_ptr);
    free(capture_buffer_ptr);
    LOG_INFO("Send thread stopped");
}

void AudioChatEngine::recvLoop() {
    LOG_INFO("Recv thread started");

    while (running_.load()) {
        try {
            network_->receive();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } catch (const std::exception& e) {
            LOG_ERROR("Error in recv thread: {}", e.what());
        }
    }

    LOG_INFO("Recv thread stopped");
}

void AudioChatEngine::playLoop() {
    LOG_INFO("Play thread started");

    int output_frames_per_buffer = config_.sample_rate * config_.frame_duration_ms / 1000;
    int max_encode_size = output_frames_per_buffer * 2;

    auto encode_buffer = reinterpret_cast<uint8_t*>(malloc(1024));
    auto decode_buffer = reinterpret_cast<int16_t*>(malloc(output_frames_per_buffer * sizeof(int16_t)));

    if (!encode_buffer || !decode_buffer) {
        LOG_ERROR("Failed to allocate buffers in play thread");
        free(encode_buffer);
        free(decode_buffer);
        return;
    }

    while (running_.load()) {
        try {
            int32_t encode_out_size = 0;
            int32_t decode_out_size = 0;

            network_->getJitterBufferPacket(encode_buffer, encode_out_size);

            if (encode_out_size > 0) {
                codec_->decode(encode_buffer, encode_out_size, decode_buffer,
                              output_frames_per_buffer * sizeof(int16_t), decode_out_size);

                if (decode_out_size > 0) {
                    audio_->play(decode_buffer, decode_out_size);
                    LOG_TRACE("Played decoded frame: size={}", decode_out_size);
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } catch (const std::exception& e) {
            LOG_ERROR("Error in play thread: {}", e.what());
        }
    }

    free(encode_buffer);
    free(decode_buffer);
    LOG_INFO("Play thread stopped");
}

void AudioChatEngine::cleanupResources() {
    LOG_DEBUG("Cleaning up resources");

    if (audio_) {
        audio_->stop();
        audio_.reset();
    }

    if (codec_) {
        codec_.reset();
    }

    if (network_) {
        network_.reset();
    }

    LOG_DEBUG("Resources cleaned up");
}
