#include <gtest/gtest.h>
#include "ConfigManager.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <filesystem>

class ConfigManagerTest : public ::testing::Test {
protected:
    ConfigManager config_manager;
};

// Test initialization
TEST_F(ConfigManagerTest, Initialization) {
    auto config = config_manager.getConfig();
    EXPECT_EQ(config.network.local_port, 10088);
    EXPECT_EQ(config.network.remote_port, 10086);
}

// Test presets
TEST_F(ConfigManagerTest, LoadPresetLowLatency) {
    EXPECT_TRUE(config_manager.loadPreset(QualityPreset::LowLatency));
    auto config = config_manager.getConfig();
    
    EXPECT_EQ(config.audio.frame_duration_ms, 10);
    EXPECT_EQ(config.codec.bitrate, 24000);
    EXPECT_EQ(config.codec.complexity, 3);
}

TEST_F(ConfigManagerTest, LoadPresetBalanced) {
    EXPECT_TRUE(config_manager.loadPreset(QualityPreset::Balanced));
    auto config = config_manager.getConfig();
    
    EXPECT_EQ(config.audio.frame_duration_ms, 20);
    EXPECT_EQ(config.codec.bitrate, 32000);
    EXPECT_EQ(config.codec.complexity, 5);
}

TEST_F(ConfigManagerTest, LoadPresetHighQuality) {
    EXPECT_TRUE(config_manager.loadPreset(QualityPreset::HighQuality));
    auto config = config_manager.getConfig();
    
    EXPECT_EQ(config.audio.frame_duration_ms, 20);
    EXPECT_EQ(config.codec.bitrate, 64000);
    EXPECT_EQ(config.codec.complexity, 10);
}

TEST_F(ConfigManagerTest, LoadPresetByName) {
    EXPECT_TRUE(config_manager.loadPresetByName("low-latency"));
    EXPECT_TRUE(config_manager.loadPresetByName("balanced"));
    EXPECT_TRUE(config_manager.loadPresetByName("high-quality"));
    
    EXPECT_FALSE(config_manager.loadPresetByName("unknown"));
}

// Test validation
TEST_F(ConfigManagerTest, ValidateValidConfig) {
    EXPECT_TRUE(config_manager.validate());
}

TEST_F(ConfigManagerTest, ValidateInvalidSampleRate) {
    auto& config = config_manager.getConfig();
    config.audio.sample_rate = 1000;  // Too low
    
    EXPECT_FALSE(config_manager.validate());
}

TEST_F(ConfigManagerTest, ValidateInvalidBitrate) {
    auto& config = config_manager.getConfig();
    config.codec.bitrate = 1000000;  // Too high
    
    EXPECT_FALSE(config_manager.validate());
}

// Test configuration modification
TEST_F(ConfigManagerTest, SetAudioConfig) {
    AudioConfig audio_config;
    audio_config.sample_rate = 48000;
    audio_config.frame_duration_ms = 20;
    
    config_manager.setAudioConfig(audio_config);
    
    auto config = config_manager.getConfig();
    EXPECT_EQ(config.audio.sample_rate, 48000);
    EXPECT_EQ(config.audio.frame_duration_ms, 20);
}

TEST_F(ConfigManagerTest, SetCodecConfig) {
    CodecConfig codec_config;
    codec_config.bitrate = 48000;
    codec_config.complexity = 8;
    
    config_manager.setCodecConfig(codec_config);
    
    auto config = config_manager.getConfig();
    EXPECT_EQ(config.codec.bitrate, 48000);
    EXPECT_EQ(config.codec.complexity, 8);
}

TEST_F(ConfigManagerTest, SetNetworkConfig) {
    NetworkConfig network_config;
    network_config.local_ip = "192.168.1.1";
    network_config.local_port = 5000;
    
    config_manager.setNetworkConfig(network_config);
    
    auto config = config_manager.getConfig();
    EXPECT_EQ(config.network.local_ip, "192.168.1.1");
    EXPECT_EQ(config.network.local_port, 5000);
}

// Test file I/O
TEST_F(ConfigManagerTest, SaveAndLoadConfiguration) {
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path test_file =
        std::filesystem::temp_directory_path() / ("audiochat-config-" + std::to_string(timestamp) + ".yaml");

    auto& config = config_manager.getConfig();
    config.network.local_ip = "10.0.0.1";
    config.network.remote_ip = "10.0.0.2";
    config.codec.bitrate = 48000;
    config.codec.enable_fec = false;
    config.codec.enable_dtx = false;
    config.codec.enable_vbr = false;
    config.audio.input_device = "Test Microphone";
    config.aec.filter_length_ms = 256;
    config.reliability.enable_nack = false;
    config.jitter_buffer.min_packets = 1;
    config.jitter_buffer.max_packets = 4;

    ASSERT_TRUE(config_manager.saveToFile(test_file.string()));

    ConfigManager config_manager2;
    ASSERT_TRUE(config_manager2.loadFromFile(test_file.string())) << config_manager2.getLastError();

    const auto loaded = config_manager2.getConfig();
    EXPECT_EQ(loaded.network.local_ip, "10.0.0.1");
    EXPECT_EQ(loaded.network.remote_ip, "10.0.0.2");
    EXPECT_EQ(loaded.codec.bitrate, 48000);
    EXPECT_FALSE(loaded.codec.enable_fec);
    EXPECT_FALSE(loaded.codec.enable_dtx);
    EXPECT_FALSE(loaded.codec.enable_vbr);
    EXPECT_EQ(loaded.audio.input_device, "Test Microphone");
    EXPECT_EQ(loaded.aec.filter_length_ms, 256);
    EXPECT_FALSE(loaded.reliability.enable_nack);
    EXPECT_EQ(loaded.jitter_buffer.min_packets, 1);
    EXPECT_EQ(loaded.jitter_buffer.max_packets, 4);

    std::filesystem::remove(test_file);
}

TEST_F(ConfigManagerTest, InvalidLoadIsTransactional) {
    const auto original = config_manager.getConfig();
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path test_file =
        std::filesystem::temp_directory_path() / ("audiochat-invalid-" + std::to_string(timestamp) + ".yaml");

    {
        std::ofstream file(test_file);
        file << "network:\n  local_port: 70000\n  local_ip: 10.0.0.1\n";
    }

    EXPECT_FALSE(config_manager.loadFromFile(test_file.string()));
    EXPECT_EQ(config_manager.getConfig().network.local_port, original.network.local_port);
    EXPECT_EQ(config_manager.getConfig().network.local_ip, original.network.local_ip);

    std::filesystem::remove(test_file);
}

// Test reset
TEST_F(ConfigManagerTest, ResetToDefaults) {
    auto& config = config_manager.getConfig();
    config.network.local_port = 9999;
    config.codec.bitrate = 99000;
    
    config_manager.reset();
    
    auto reset_config = config_manager.getConfig();
    EXPECT_EQ(reset_config.network.local_port, 10088);
    EXPECT_EQ(reset_config.codec.bitrate, 32000);
}

// Test available presets
TEST_F(ConfigManagerTest, GetAvailablePresets) {
    auto presets = config_manager.getAvailablePresets();
    
    EXPECT_EQ(presets.size(), 3);
    EXPECT_NE(std::find(presets.begin(), presets.end(), "low-latency"), presets.end());
    EXPECT_NE(std::find(presets.begin(), presets.end(), "balanced"), presets.end());
    EXPECT_NE(std::find(presets.begin(), presets.end(), "high-quality"), presets.end());
}

