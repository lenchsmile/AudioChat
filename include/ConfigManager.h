#pragma once

#include <string>
#include <map>
#include <memory>
#include <vector>
#include <cstdint>

/**
 * @struct AudioConfig
 * @brief Audio device and codec configuration
 */
struct AudioConfig {
    uint32_t sample_rate = 48000;
    uint32_t frame_duration_ms = 20;
    std::string input_device = "auto";
    std::string output_device = "auto";
    double volume = 1.0;
};

/**
 * @struct CodecConfig
 * @brief Codec parameters
 */
struct CodecConfig {
    uint32_t bitrate = 32000;
    uint32_t complexity = 5;
    bool enable_fec = true;
    bool enable_dtx = true;
    bool enable_vbr = true;
};

/**
 * @struct AecConfig
 * @brief Acoustic Echo Cancellation configuration
 */
struct AecConfigSettings {
    bool enable = true;
    uint32_t filter_length_ms = 128; ///< Reference-buffer capacity; not the WebRTC adaptive filter length
    uint32_t suppression_level = 2; ///< Reserved compatibility value; current AEC3 tuning is fixed
};

/**
 * @struct NetworkConfig
 * @brief Network connection settings
 */
struct NetworkConfig {
    std::string local_ip = "127.0.0.1";
    uint16_t local_port = 10088;
    std::string remote_ip = "127.0.0.1";
    uint16_t remote_port = 10086;
    bool enable_heartbeat = true;
    uint32_t heartbeat_interval_ms = 1000;
};

/**
 * @struct ReliabilityConfig
 * @brief Network reliability settings
 */
struct ReliabilityConfig {
    bool enable_monitoring = true;
    bool enable_congestion_control = true;
    bool enable_nack = true;
    double congestion_loss_threshold = 5.0;
    double congestion_rtt_threshold = 200.0;
};

/**
 * @struct JitterBufferConfig
 * @brief Jitter buffer settings
 */
struct JitterBufferConfig {
    uint32_t min_packets = 2;
    uint32_t max_packets = 10;
    bool adaptive = true;
};

/**
 * @struct ApplicationConfig
 * @brief Complete application configuration
 */
struct ApplicationConfig {
    std::string name = "AudioChat";
    std::string version = "0.5.0";
    
    AudioConfig audio;
    CodecConfig codec;
    AecConfigSettings aec;
    NetworkConfig network;
    ReliabilityConfig reliability;
    JitterBufferConfig jitter_buffer;
    
    std::string preset = "balanced";  // Current preset
};

/**
 * @enum QualityPreset
 * @brief Quality preset types
 */
enum class QualityPreset {
    LowLatency,      // Minimum latency, lower quality
    Balanced,        // Default balanced
    HighQuality      // Maximum quality, higher latency
};

/**
 * @class ConfigManager
 * @brief Manage application configuration
 * 
 * Features:
 * - Load/save YAML configuration
 * - Quality presets
 * - Configuration validation
 * - Default values
 * - Command-line overrides
 */
class ConfigManager {
public:
    ConfigManager();
    ~ConfigManager();
    
    /**
     * @brief Load configuration from file
     * @param config_file Path to configuration file
     * @return True if successful
     */
    bool loadFromFile(const std::string& config_file);
    
    /**
     * @brief Save configuration to file
     * @param config_file Path to save to
     * @return True if successful
     */
    bool saveToFile(const std::string& config_file);
    
    /**
     * @brief Load built-in preset
     * @param preset Quality preset
     * @return True if successful
     */
    bool loadPreset(QualityPreset preset);
    
    /**
     * @brief Load preset by name
     * @param preset_name "low-latency", "balanced", or "high-quality"
     * @return True if successful
     */
    bool loadPresetByName(const std::string& preset_name);
    
    /**
     * @brief Get current configuration
     * @return Reference to ApplicationConfig
     */
    const ApplicationConfig& getConfig() const;
    
    /**
     * @brief Get mutable configuration
     * @return Mutable reference to ApplicationConfig
     */
    ApplicationConfig& getConfig();
    
    /**
     * @brief Set audio configuration
     * @param config Audio configuration
     */
    void setAudioConfig(const AudioConfig& config);
    
    /**
     * @brief Set codec configuration
     * @param config Codec configuration
     */
    void setCodecConfig(const CodecConfig& config);
    
    /**
     * @brief Set network configuration
     * @param config Network configuration
     */
    void setNetworkConfig(const NetworkConfig& config);
    
    /**
     * @brief Validate current configuration
     * @return True if valid
     */
    bool validate();
    
    /**
     * @brief Get validation error message
     * @return Error message if validation failed
     */
    std::string getLastError() const;
    
    /**
     * @brief Reset to defaults
     */
    void reset();
    
    /**
     * @brief Get list of available presets
     * @return Vector of preset names
     */
    std::vector<std::string> getAvailablePresets() const;

private:
    ApplicationConfig config_;
    mutable std::string last_error_;
    
    // Helper methods
    void setDefaults();
    void applyPresetLowLatency();
    void applyPresetBalanced();
    void applyPresetHighQuality();
    bool validateAudioConfig() const;
    bool validateNetworkConfig() const;
    bool validateCodecConfig() const;
    
    // YAML parsing helpers
    bool parseYamlFile(const std::string& file_path);
    bool writeYamlFile(const std::string& file_path) const;
};

