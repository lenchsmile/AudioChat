#pragma once

#include "AecAudioHandler.h"
#include "AudioHandler.h"
#include "Codec.h"
#include "NetworkHandler.h"
#include "Result.h"
#include "Logger.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

struct AudioChatConfig {
    std::string local_ip = "127.0.0.1";
    int local_port = 10088;
    std::string remote_ip = "127.0.0.1";
    int remote_port = 10086;

    int sample_rate = 48000;
    int frame_duration_ms = 20;
    std::string input_device;
    std::string output_device;

    int bitrate = 24000;
    int complexity = 5;
    bool enable_fec = true;
    bool enable_dtx = false;
    int packet_loss_perc = 10;

    bool enable_aec = false;
    int aec_suppression_level = 2;
    bool enable_ns = true;
    bool enable_agc = true;
    int reference_delay_ms = 30;
    int reference_buffer_ms = 128;
    bool allow_aec_passthrough = false;
};

class AudioChatEngine {
public:
    explicit AudioChatEngine(const AudioChatConfig& config);
    ~AudioChatEngine();

    AudioChatEngine(const AudioChatEngine&) = delete;
    AudioChatEngine& operator=(const AudioChatEngine&) = delete;
    AudioChatEngine(AudioChatEngine&&) = delete;
    AudioChatEngine& operator=(AudioChatEngine&&) = delete;

    Result<void> init();
    Result<void> start();
    Result<void> stop();
    bool isRunning() const { return running_.load(); }
    bool isAecActive();
    std::string getAecStatistics();
    bool hasFatalError() const;
    std::string getFatalError() const;
    const AudioChatConfig& getConfig() const { return config_; }

private:
    Result<void> initComponents();
    Result<void> startThreads();
    Result<void> stopThreads();
    void cleanupResources();
    void requestStop(const std::string& error);
    void joinThreads();

    void sendLoop();
    void recvLoop();
    void playLoop();

    AudioChatConfig config_;
    std::unique_ptr<AudioChat::AecAudioHandler> audio_;
    std::unique_ptr<Codec> codec_;
    std::unique_ptr<NetworkHandler> network_;

    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};

    std::thread send_thread_;
    std::thread recv_thread_;
    std::thread play_thread_;

    std::mutex lifecycle_mutex_;
    std::mutex work_mutex_;
    mutable std::mutex fatal_error_mutex_;
    std::string fatal_error_;
    std::condition_variable work_cv_;
};
