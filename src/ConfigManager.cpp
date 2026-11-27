#include "ConfigManager.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

ConfigManager::ConfigManager() {
    setDefaults();
}

ConfigManager::~ConfigManager() = default;

bool ConfigManager::loadFromFile(const std::string& config_file) {
    std::ifstream file(config_file);
    if (!file.is_open()) {
        last_error_ = "Cannot open file: " + config_file;
        return false;
    }
    
    return parseYamlFile(config_file);
}

bool ConfigManager::saveToFile(const std::string& config_file) {
    if (!validate()) {
        last_error_ = "Configuration validation failed before save";
        return false;
    }
    
    return writeYamlFile(config_file);
}

bool ConfigManager::loadPreset(QualityPreset preset) {
    switch (preset) {
        case QualityPreset::LowLatency:
            applyPresetLowLatency();
            config_.preset = "low-latency";
            return true;
            
        case QualityPreset::Balanced:
            applyPresetBalanced();
            config_.preset = "balanced";
            return true;
            
        case QualityPreset::HighQuality:
            applyPresetHighQuality();
            config_.preset = "high-quality";
            return true;
            
        default:
            last_error_ = "Unknown preset";
            return false;
    }
}

bool ConfigManager::loadPresetByName(const std::string& preset_name) {
    std::string lower_name = preset_name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    
    if (lower_name == "low-latency" || lower_name == "lowlatency") {
        return loadPreset(QualityPreset::LowLatency);
    } else if (lower_name == "balanced") {
        return loadPreset(QualityPreset::Balanced);
    } else if (lower_name == "high-quality" || lower_name == "highquality") {
        return loadPreset(QualityPreset::HighQuality);
    } else {
        last_error_ = "Unknown preset: " + preset_name;
        return false;
    }
}

const ApplicationConfig& ConfigManager::getConfig() const {
    return config_;
}

ApplicationConfig& ConfigManager::getConfig() {
    return config_;
}

void ConfigManager::setAudioConfig(const AudioConfig& config) {
    config_.audio = config;
}

void ConfigManager::setCodecConfig(const CodecConfig& config) {
    config_.codec = config;
}

void ConfigManager::setNetworkConfig(const NetworkConfig& config) {
    config_.network = config;
}

bool ConfigManager::validate() {
    // Reset error
    last_error_.clear();
    
    if (!validateAudioConfig()) {
        return false;
    }
    
    if (!validateNetworkConfig()) {
        return false;
    }
    
    if (!validateCodecConfig()) {
        return false;
    }
    
    return true;
}

std::string ConfigManager::getLastError() const {
    return last_error_;
}

void ConfigManager::reset() {
    setDefaults();
}

std::vector<std::string> ConfigManager::getAvailablePresets() const {
    return {"low-latency", "balanced", "high-quality"};
}

void ConfigManager::setDefaults() {
    // Network defaults
    config_.network.local_ip = "127.0.0.1";
    config_.network.local_port = 10088;
    config_.network.remote_ip = "127.0.0.1";
    config_.network.remote_port = 10086;
    config_.network.enable_heartbeat = true;
    config_.network.heartbeat_interval_ms = 1000;
    
    // Audio defaults
    config_.audio.sample_rate = 48000;
    config_.audio.frame_duration_ms = 20;
    config_.audio.input_device = "auto";
    config_.audio.output_device = "auto";
    config_.audio.volume = 1.0;
    
    // Codec defaults
    config_.codec.bitrate = 32000;
    config_.codec.complexity = 5;
    config_.codec.enable_fec = true;
    config_.codec.enable_dtx = true;
    config_.codec.enable_vbr = true;
    
    // AEC defaults
    config_.aec.enable = true;
    config_.aec.filter_length_ms = 128;
    config_.aec.suppression_level = 2;
    
    // Reliability defaults
    config_.reliability.enable_monitoring = true;
    config_.reliability.enable_congestion_control = true;
    config_.reliability.enable_nack = true;
    config_.reliability.congestion_loss_threshold = 5.0;
    config_.reliability.congestion_rtt_threshold = 200.0;
    
    // Jitter buffer defaults
    config_.jitter_buffer.min_packets = 2;
    config_.jitter_buffer.max_packets = 10;
    config_.jitter_buffer.adaptive = true;
    
    config_.preset = "balanced";
}

void ConfigManager::applyPresetLowLatency() {
    setDefaults();
    
    // Low latency configuration
    config_.audio.frame_duration_ms = 10;      // Shorter frames
    config_.codec.bitrate = 24000;             // Lower bitrate
    config_.codec.complexity = 3;              // Lower complexity
    config_.aec.suppression_level = 1;         // Moderate AEC
    config_.jitter_buffer.min_packets = 1;     // Minimal buffering
    config_.jitter_buffer.max_packets = 3;
    config_.reliability.congestion_loss_threshold = 10.0;  // Higher threshold
}

void ConfigManager::applyPresetBalanced() {
    setDefaults();
    // Already default = balanced
}

void ConfigManager::applyPresetHighQuality() {
    setDefaults();
    
    // High quality configuration
    config_.audio.frame_duration_ms = 40;      // Longer frames
    config_.codec.bitrate = 64000;             // Higher bitrate
    config_.codec.complexity = 10;             // Maximum complexity
    config_.codec.enable_vbr = true;           // Variable bitrate
    config_.aec.suppression_level = 2;         // High AEC
    config_.aec.filter_length_ms = 256;        // Longer filter
    config_.jitter_buffer.min_packets = 3;     // More buffering
    config_.jitter_buffer.max_packets = 15;
    config_.jitter_buffer.adaptive = true;
}

bool ConfigManager::validateAudioConfig() const {
    if (config_.audio.sample_rate < 8000 || config_.audio.sample_rate > 192000) {
        last_error_ = "Invalid sample rate: " + std::to_string(config_.audio.sample_rate);
        return false;
    }
    
    if (config_.audio.frame_duration_ms < 5 || config_.audio.frame_duration_ms > 100) {
        last_error_ = "Invalid frame duration: " + std::to_string(config_.audio.frame_duration_ms);
        return false;
    }
    
    if (config_.audio.volume < 0.0 || config_.audio.volume > 2.0) {
        last_error_ = "Invalid volume: " + std::to_string(config_.audio.volume);
        return false;
    }
    
    return true;
}

bool ConfigManager::validateNetworkConfig() const {
    if (config_.network.local_port == 0) {
        last_error_ = "Invalid local port: 0";
        return false;
    }
    
    if (config_.network.remote_port == 0) {
        last_error_ = "Invalid remote port: 0";
        return false;
    }
    
    if (config_.network.heartbeat_interval_ms < 100 || 
        config_.network.heartbeat_interval_ms > 60000) {
        last_error_ = "Invalid heartbeat interval: " + 
                     std::to_string(config_.network.heartbeat_interval_ms);
        return false;
    }
    
    return true;
}

bool ConfigManager::validateCodecConfig() const {
    if (config_.codec.bitrate < 8000 || config_.codec.bitrate > 128000) {
        last_error_ = "Invalid bitrate: " + std::to_string(config_.codec.bitrate);
        return false;
    }
    
    if (config_.codec.complexity < 0 || config_.codec.complexity > 10) {
        last_error_ = "Invalid complexity: " + std::to_string(config_.codec.complexity);
        return false;
    }
    
    return true;
}

bool ConfigManager::parseYamlFile(const std::string& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        last_error_ = "Cannot open file: " + file_path;
        return false;
    }
    
    std::string line;
    std::string current_section;
    
    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);
        
        // Simple YAML parsing (non-recursive)
        if (line.find(':') != std::string::npos) {
            size_t colon_pos = line.find(':');
            std::string key = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 1);
            
            // Trim
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            
            // Parse known keys (simplified approach)
            if (key == "local_ip") {
                config_.network.local_ip = value;
            } else if (key == "local_port") {
                config_.network.local_port = static_cast<uint16_t>(std::stoi(value));
            } else if (key == "remote_ip") {
                config_.network.remote_ip = value;
            } else if (key == "remote_port") {
                config_.network.remote_port = static_cast<uint16_t>(std::stoi(value));
            } else if (key == "bitrate") {
                config_.codec.bitrate = std::stoi(value);
            } else if (key == "complexity") {
                config_.codec.complexity = std::stoi(value);
            } else if (key == "sample_rate") {
                config_.audio.sample_rate = std::stoi(value);
            } else if (key == "frame_duration_ms") {
                config_.audio.frame_duration_ms = std::stoi(value);
            }
        }
    }
    
    return validate();
}

bool ConfigManager::writeYamlFile(const std::string& file_path) const {
    std::ofstream file(file_path);
    if (!file.is_open()) {
        last_error_ = "Cannot write to file: " + file_path;
        return false;
    }
    
    file << "# AudioChat Configuration\n\n";
    
    file << "application:\n";
    file << "  name: \"" << config_.name << "\"\n";
    file << "  version: \"" << config_.version << "\"\n\n";
    
    file << "network:\n";
    file << "  local_ip: \"" << config_.network.local_ip << "\"\n";
    file << "  local_port: " << config_.network.local_port << "\n";
    file << "  remote_ip: \"" << config_.network.remote_ip << "\"\n";
    file << "  remote_port: " << config_.network.remote_port << "\n";
    file << "  enable_heartbeat: " << (config_.network.enable_heartbeat ? "true" : "false") << "\n";
    file << "  heartbeat_interval_ms: " << config_.network.heartbeat_interval_ms << "\n\n";
    
    file << "audio:\n";
    file << "  sample_rate: " << config_.audio.sample_rate << "\n";
    file << "  frame_duration_ms: " << config_.audio.frame_duration_ms << "\n";
    file << "  input_device: \"" << config_.audio.input_device << "\"\n";
    file << "  output_device: \"" << config_.audio.output_device << "\"\n";
    file << "  volume: " << config_.audio.volume << "\n\n";
    
    file << "codec:\n";
    file << "  bitrate: " << config_.codec.bitrate << "\n";
    file << "  complexity: " << config_.codec.complexity << "\n";
    file << "  enable_fec: " << (config_.codec.enable_fec ? "true" : "false") << "\n";
    file << "  enable_dtx: " << (config_.codec.enable_dtx ? "true" : "false") << "\n";
    file << "  enable_vbr: " << (config_.codec.enable_vbr ? "true" : "false") << "\n\n";
    
    file << "aec:\n";
    file << "  enable: " << (config_.aec.enable ? "true" : "false") << "\n";
    file << "  filter_length_ms: " << config_.aec.filter_length_ms << "\n";
    file << "  suppression_level: " << config_.aec.suppression_level << "\n\n";
    
    file << "network_reliability:\n";
    file << "  enable_monitoring: " << (config_.reliability.enable_monitoring ? "true" : "false") << "\n";
    file << "  enable_congestion_control: " << (config_.reliability.enable_congestion_control ? "true" : "false") << "\n";
    file << "  enable_nack: " << (config_.reliability.enable_nack ? "true" : "false") << "\n\n";
    
    file << "presets:\n";
    file << "  default: \"" << config_.preset << "\"\n";
    
    return true;
}

