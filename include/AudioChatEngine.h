#pragma once

#include "AudioHandler.h"
#include "Codec.h"
#include "NetworkHandler.h"
#include "Result.h"
#include "Logger.h"

#include <memory>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>

/**
 * @brief Audio Chat Engine configuration
 */
struct AudioChatConfig {
    // Network configuration
    std::string local_ip = "127.0.0.1";
    int local_port = 10088;
    std::string remote_ip = "127.0.0.1";
    int remote_port = 10086;
    
    // Audio configuration
    int sample_rate = 48000;
    int frame_duration_ms = 20;
    std::string input_device;  // Empty for auto-select
    std::string output_device; // Empty for auto-select
    
    // Codec configuration
    int bitrate = 24000;
    int complexity = 5;
    bool enable_fec = true;
    bool enable_dtx = false;
    int packet_loss_perc = 10;
};

/**
 * @brief Main AudioChat engine managing all components
 * 
 * This class orchestrates:
 * - Audio input/output via AudioHandler
 * - Network communication via NetworkHandler
 * - Audio encoding/decoding via Codec
 * - Thread management (send, receive, play)
 * 
 * Key improvements over previous implementation:
 * - Uses joinable threads (not detached)
 * - RAII resource management
 * - Graceful shutdown with proper thread synchronization
 * - Logging and error handling
 */
class AudioChatEngine {
public:
    /**
     * @brief Constructor
     * @param config Engine configuration
     */
    explicit AudioChatEngine(const AudioChatConfig& config);

    /**
     * @brief Destructor - ensures proper cleanup
     */
    ~AudioChatEngine();

    /**
     * @brief Initialize all components
     * @return Result indicating success or error
     */
    Result<void> init();

    /**
     * @brief Start the communication engine
     * @return Result indicating success or error
     */
    Result<void> start();

    /**
     * @brief Stop the communication engine gracefully
     * @return Result indicating success or error
     */
    Result<void> stop();

    /**
     * @brief Check if engine is running
     * @return true if running, false otherwise
     */
    bool isRunning() const { return running_.load(); }

    /**
     * @brief Get configuration
     * @return Current configuration
     */
    const AudioChatConfig& getConfig() const { return config_; }

    // Deleted copy/move operations
    AudioChatEngine(const AudioChatEngine&) = delete;
    AudioChatEngine(AudioChatEngine&&) = delete;
    AudioChatEngine& operator=(const AudioChatEngine&) = delete;
    AudioChatEngine& operator=(AudioChatEngine&&) = delete;

private:
    // Configuration
    AudioChatConfig config_;

    // Components
    std::unique_ptr<AudioHandler> audio_;
    std::unique_ptr<Codec> codec_;
    std::unique_ptr<NetworkHandler> network_;

    // Thread management
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};
    
    std::thread send_thread_;
    std::thread recv_thread_;
    std::thread play_thread_;

    std::mutex shutdown_mutex_;
    std::condition_variable shutdown_cv_;

    // Thread routines
    void sendLoop();
    void recvLoop();
    void playLoop();

    // Utility methods
    Result<void> initComponents();
    Result<void> startThreads();
    Result<void> stopThreads();
    void cleanupResources();
};
