#include "ConfigManager.h"
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace {

bool isValidIPv4(const std::string& value) {
    int part = 0;
    int digits = 0;
    int separators = 0;
    for (char ch : value) {
        if (ch == '.') {
            if (digits == 0 || part > 255) {
                return false;
            }
            part = 0;
            digits = 0;
            ++separators;
            continue;
        }
        if (ch < '0' || ch > '9') {
            return false;
        }
        part = part * 10 + (ch - '0');
        ++digits;
        if (digits > 3 || part > 255) {
            return false;
        }
    }
    return separators == 3 && digits > 0 && part <= 255;
}

} // namespace

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
    config_.name = "AudioChat";
    config_.version = "0.5.0";

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
    config_.audio.frame_duration_ms = 20;      // Highest quality at a supported frame size
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
    if (config_.audio.sample_rate != 8000 && config_.audio.sample_rate != 16000 &&
        config_.audio.sample_rate != 32000 && config_.audio.sample_rate != 48000) {
        last_error_ = "Invalid sample rate: " + std::to_string(config_.audio.sample_rate) +
                      " (supported: 8000, 16000, 32000, 48000)";
        return false;
    }

    if (config_.audio.frame_duration_ms != 10 && config_.audio.frame_duration_ms != 20) {
        last_error_ = "Invalid frame duration: " + std::to_string(config_.audio.frame_duration_ms) +
                      " (supported: 10, 20)";
        return false;
    }

    if (!std::isfinite(config_.audio.volume) || config_.audio.volume < 0.0 ||
        config_.audio.volume > 2.0) {
        last_error_ = "Invalid volume";
        return false;
    }

    return true;
}

bool ConfigManager::validateNetworkConfig() const {
    if (!isValidIPv4(config_.network.local_ip)) {
        last_error_ = "Invalid local IPv4 address: " + config_.network.local_ip;
        return false;
    }
    if (!isValidIPv4(config_.network.remote_ip)) {
        last_error_ = "Invalid remote IPv4 address: " + config_.network.remote_ip;
        return false;
    }

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

    if (!std::isfinite(config_.reliability.congestion_loss_threshold) ||
        config_.reliability.congestion_loss_threshold <= 0.0 ||
        config_.reliability.congestion_loss_threshold > 100.0) {
        last_error_ = "Invalid congestion loss threshold";
        return false;
    }

    if (!std::isfinite(config_.reliability.congestion_rtt_threshold) ||
        config_.reliability.congestion_rtt_threshold <= 0.0) {
        last_error_ = "Invalid congestion RTT threshold";
        return false;
    }

    if (config_.jitter_buffer.min_packets == 0 ||
        config_.jitter_buffer.max_packets < config_.jitter_buffer.min_packets) {
        last_error_ = "Invalid jitter buffer limits";
        return false;
    }

    if (config_.aec.suppression_level > 2) {
        last_error_ = "Invalid AEC suppression level";
        return false;
    }

    return true;
}

bool ConfigManager::validateCodecConfig() const {
    if (config_.codec.bitrate < 8000 || config_.codec.bitrate > 128000) {
        last_error_ = "Invalid bitrate: " + std::to_string(config_.codec.bitrate);
        return false;
    }
    
    if (config_.codec.complexity > 10) {
        last_error_ = "Invalid complexity: " + std::to_string(config_.codec.complexity);
        return false;
    }
    
    return true;
}

namespace {

template <typename T>
bool readYamlValue(const YAML::Node& section, const char* key, T& target,
                   const std::string& path, std::string& error) {
    if (!section || !section[key]) {
        return true;
    }

    try {
        target = section[key].as<T>();
        return true;
    } catch (const YAML::Exception& ex) {
        error = "Invalid value for " + path + "." + key + ": " + ex.what();
        return false;
    }
}

template <typename T>
bool readBoundedUnsigned(const YAML::Node& section, const char* key, T& target,
                         uint64_t maximum, const std::string& path,
                         std::string& error) {
    if (!section || !section[key]) {
        return true;
    }

    uint64_t value = 0;
    if (!readYamlValue(section, key, value, path, error)) {
        return false;
    }
    if (value > maximum) {
        error = "Value out of range for " + path + "." + key;
        return false;
    }
    target = static_cast<T>(value);
    return true;
}

} // namespace

bool ConfigManager::parseYamlFile(const std::string& file_path) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        last_error_ = "Cannot open file: " + file_path;
        return false;
    }

    YAML::Node root;
    try {
        root = YAML::Load(file);
    } catch (const YAML::Exception& ex) {
        last_error_ = "Failed to parse YAML: " + std::string(ex.what());
        return false;
    }

    if (!root || !root.IsMap()) {
        last_error_ = "Configuration root must be a YAML mapping";
        return false;
    }

    const char* section_names[] = {
        "application", "audio", "codec", "aec", "network",
        "network_reliability", "jitter_buffer", "presets"
    };
    for (const char* name : section_names) {
        if (root[name] && !root[name].IsMap()) {
            last_error_ = std::string("Configuration section must be a mapping: ") + name;
            return false;
        }
    }

    ApplicationConfig candidate = config_;
    std::string error;

    if (!readYamlValue(root["application"], "name", candidate.name, "application", error) ||
        !readYamlValue(root["application"], "version", candidate.version, "application", error) ||
        !readBoundedUnsigned(root["audio"], "sample_rate", candidate.audio.sample_rate,
                             (std::numeric_limits<uint32_t>::max)(), "audio", error) ||
        !readBoundedUnsigned(root["audio"], "frame_duration_ms", candidate.audio.frame_duration_ms,
                             (std::numeric_limits<uint32_t>::max)(), "audio", error) ||
        !readYamlValue(root["audio"], "input_device", candidate.audio.input_device, "audio", error) ||
        !readYamlValue(root["audio"], "output_device", candidate.audio.output_device, "audio", error) ||
        !readYamlValue(root["audio"], "volume", candidate.audio.volume, "audio", error) ||
        !readBoundedUnsigned(root["codec"], "bitrate", candidate.codec.bitrate,
                             (std::numeric_limits<uint32_t>::max)(), "codec", error) ||
        !readBoundedUnsigned(root["codec"], "complexity", candidate.codec.complexity,
                             (std::numeric_limits<uint32_t>::max)(), "codec", error) ||
        !readYamlValue(root["codec"], "enable_fec", candidate.codec.enable_fec, "codec", error) ||
        !readYamlValue(root["codec"], "enable_dtx", candidate.codec.enable_dtx, "codec", error) ||
        !readYamlValue(root["codec"], "enable_vbr", candidate.codec.enable_vbr, "codec", error) ||
        !readYamlValue(root["aec"], "enable", candidate.aec.enable, "aec", error) ||
        !readBoundedUnsigned(root["aec"], "filter_length_ms", candidate.aec.filter_length_ms,
                             (std::numeric_limits<uint32_t>::max)(), "aec", error) ||
        !readBoundedUnsigned(root["aec"], "suppression_level", candidate.aec.suppression_level,
                             2, "aec", error) ||
        !readYamlValue(root["network"], "local_ip", candidate.network.local_ip, "network", error) ||
        !readBoundedUnsigned(root["network"], "local_port", candidate.network.local_port,
                             65535, "network", error) ||
        !readYamlValue(root["network"], "remote_ip", candidate.network.remote_ip, "network", error) ||
        !readBoundedUnsigned(root["network"], "remote_port", candidate.network.remote_port,
                             65535, "network", error) ||
        !readYamlValue(root["network"], "enable_heartbeat", candidate.network.enable_heartbeat,
                       "network", error) ||
        !readBoundedUnsigned(root["network"], "heartbeat_interval_ms",
                             candidate.network.heartbeat_interval_ms,
                             (std::numeric_limits<uint32_t>::max)(), "network", error) ||
        !readYamlValue(root["network_reliability"], "enable_monitoring",
                       candidate.reliability.enable_monitoring, "network_reliability", error) ||
        !readYamlValue(root["network_reliability"], "enable_congestion_control",
                       candidate.reliability.enable_congestion_control, "network_reliability", error) ||
        !readYamlValue(root["network_reliability"], "enable_nack",
                       candidate.reliability.enable_nack, "network_reliability", error) ||
        !readYamlValue(root["network_reliability"], "congestion_loss_threshold",
                       candidate.reliability.congestion_loss_threshold, "network_reliability", error) ||
        !readYamlValue(root["network_reliability"], "congestion_rtt_threshold",
                       candidate.reliability.congestion_rtt_threshold, "network_reliability", error) ||
        !readBoundedUnsigned(root["jitter_buffer"], "min_packets",
                             candidate.jitter_buffer.min_packets,
                             (std::numeric_limits<uint32_t>::max)(), "jitter_buffer", error) ||
        !readBoundedUnsigned(root["jitter_buffer"], "max_packets",
                             candidate.jitter_buffer.max_packets,
                             (std::numeric_limits<uint32_t>::max)(), "jitter_buffer", error) ||
        !readYamlValue(root["jitter_buffer"], "adaptive", candidate.jitter_buffer.adaptive,
                       "jitter_buffer", error) ||
        !readYamlValue(root["presets"], "default", candidate.preset, "presets", error)) {
        last_error_ = error;
        return false;
    }

    const ApplicationConfig previous = config_;
    config_ = std::move(candidate);
    if (!validate()) {
        config_ = previous;
        return false;
    }

    return true;
}

bool ConfigManager::writeYamlFile(const std::string& file_path) const {
    std::ofstream file(file_path);
    if (!file.is_open()) {
        last_error_ = "Cannot write to file: " + file_path;
        return false;
    }

    YAML::Node root;
    root["application"]["name"] = config_.name;
    root["application"]["version"] = config_.version;

    root["audio"]["sample_rate"] = config_.audio.sample_rate;
    root["audio"]["frame_duration_ms"] = config_.audio.frame_duration_ms;
    root["audio"]["input_device"] = config_.audio.input_device;
    root["audio"]["output_device"] = config_.audio.output_device;
    root["audio"]["volume"] = config_.audio.volume;

    root["codec"]["bitrate"] = config_.codec.bitrate;
    root["codec"]["complexity"] = config_.codec.complexity;
    root["codec"]["enable_fec"] = config_.codec.enable_fec;
    root["codec"]["enable_dtx"] = config_.codec.enable_dtx;
    root["codec"]["enable_vbr"] = config_.codec.enable_vbr;

    root["aec"]["enable"] = config_.aec.enable;
    root["aec"]["filter_length_ms"] = config_.aec.filter_length_ms;
    root["aec"]["suppression_level"] = config_.aec.suppression_level;

    root["network"]["local_ip"] = config_.network.local_ip;
    root["network"]["local_port"] = config_.network.local_port;
    root["network"]["remote_ip"] = config_.network.remote_ip;
    root["network"]["remote_port"] = config_.network.remote_port;
    root["network"]["enable_heartbeat"] = config_.network.enable_heartbeat;
    root["network"]["heartbeat_interval_ms"] = config_.network.heartbeat_interval_ms;

    root["network_reliability"]["enable_monitoring"] = config_.reliability.enable_monitoring;
    root["network_reliability"]["enable_congestion_control"] =
        config_.reliability.enable_congestion_control;
    root["network_reliability"]["enable_nack"] = config_.reliability.enable_nack;
    root["network_reliability"]["congestion_loss_threshold"] =
        config_.reliability.congestion_loss_threshold;
    root["network_reliability"]["congestion_rtt_threshold"] =
        config_.reliability.congestion_rtt_threshold;

    root["jitter_buffer"]["min_packets"] = config_.jitter_buffer.min_packets;
    root["jitter_buffer"]["max_packets"] = config_.jitter_buffer.max_packets;
    root["jitter_buffer"]["adaptive"] = config_.jitter_buffer.adaptive;
    root["presets"]["default"] = config_.preset;

    file << "# AudioChat Configuration\n" << root;
    return static_cast<bool>(file);
}

