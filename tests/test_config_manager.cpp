#include <gtest/gtest.h>
#include "ConfigManager.h"
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
    
    EXPECT_EQ(config.audio.frame_duration_ms, 40);
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
    audio_config.sample_rate = 44100;
    audio_config.frame_duration_ms = 30;
    
    config_manager.setAudioConfig(audio_config);
    
    auto config = config_manager.getConfig();
    EXPECT_EQ(config.audio.sample_rate, 44100);
    EXPECT_EQ(config.audio.frame_duration_ms, 30);
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
    std::string test_file = "test_config.yaml";
    
    // Modify and save
    auto& config = config_manager.getConfig();
    config.network.local_ip = "10.0.0.1";
    config.codec.bitrate = 48000;
    
    EXPECT_TRUE(config_manager.saveToFile(test_file));
    
    // Create new manager and load
    ConfigManager config_manager2;
    EXPECT_TRUE(config_manager2.loadFromFile(test_file));
    
    auto loaded_config = config_manager2.getConfig();
    EXPECT_EQ(loaded_config.network.local_ip, "10.0.0.1");
    EXPECT_EQ(loaded_config.codec.bitrate, 48000);
    
    // Cleanup
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

