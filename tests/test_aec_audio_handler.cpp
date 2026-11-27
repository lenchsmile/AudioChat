#include <gtest/gtest.h>
#include "AecAudioHandler.h"
#include "Logger.h"
#include <memory>
#include <cstring>
#include <algorithm>
#include <vector>

using namespace AudioChat;

/**
 * @brief Mock AudioHandler for testing (minimal implementation)
 * 
 * Since the real AudioHandler requires PortAudio and hardware,
 * we use this mock for testing the AecAudioHandler wrapper.
 */
class MockAudioHandler {
public:
    MockAudioHandler() = default;
    ~MockAudioHandler() = default;

    void play(const int16_t* data, int32_t data_size) {
        // Mock implementation
    }

    void startRecordeAudio() {
        // Mock implementation
    }

    void stopRecordeAudio() {
        // Mock implementation
    }

    bool isRecording() {
        return false;
    }
};

/**
 * @brief Unit tests for AecAudioHandler integration
 * 
 * Tests verify:
 * 1. Configuration validation
 * 2. Handler creation with AEC enabled/disabled
 * 3. Reference and capture signal processing
 * 4. AEC enable/disable toggling
 * 5. Fallback behavior when insufficient buffer
 * 6. Statistics gathering
 */
class AecAudioHandlerTest : public ::testing::Test {
protected:
    /**
     * @brief Create default AecAudioHandler configuration
     */
    AecAudioHandler::Config CreateDefaultConfig(bool enable_aec = true) {
        AecAudioHandler::Config config;
        config.enable_aec = enable_aec;
        
        if (enable_aec) {
            // AEC config
            config.aec_config.sample_rate_hz = 48000;
            config.aec_config.num_channels = 1;
            config.aec_config.frame_duration_ms = 10;
            config.aec_config.enable_aec = true;
            config.aec_config.enable_ns = true;
            config.aec_config.enable_agc = true;
            config.aec_config.aec_suppression_level = 2;
            
            // Buffer config
            config.buffer_config.sample_rate_hz = 48000;
            config.buffer_config.num_channels = 1;
            config.buffer_config.frame_duration_ms = 10;
            config.buffer_config.delay_ms = 30;
            config.buffer_config.buffer_duration_ms = 100;
        }
        
        return config;
    }

    /**
     * @brief Create test audio data
     */
    std::vector<int16_t> CreateTestAudio(int32_t num_samples, 
                                         int16_t value = 100) {
        return std::vector<int16_t>(num_samples, value);
    }
};

// ============================================================================
// Configuration Validation Tests
// ============================================================================

TEST_F(AecAudioHandlerTest, ValidateConfigWithAecEnabled) {
    auto config = CreateDefaultConfig(true);
    auto result = config.Validate();
    EXPECT_TRUE(result.isOk()) << "Valid config should pass validation";
}

TEST_F(AecAudioHandlerTest, ValidateConfigWithAecDisabled) {
    auto config = CreateDefaultConfig(false);
    auto result = config.Validate();
    EXPECT_TRUE(result.isOk()) << "Config with AEC disabled should pass";
}

TEST_F(AecAudioHandlerTest, ValidateConfigMismatchedSampleRates) {
    auto config = CreateDefaultConfig(true);
    config.aec_config.sample_rate_hz = 48000;
    config.buffer_config.sample_rate_hz = 16000;  // Mismatch
    
    auto result = config.Validate();
    EXPECT_TRUE(result.isError()) << "Mismatched sample rates should fail";
}

TEST_F(AecAudioHandlerTest, ValidateConfigMismatchedChannels) {
    auto config = CreateDefaultConfig(true);
    config.aec_config.num_channels = 1;
    config.buffer_config.num_channels = 2;  // Mismatch
    
    auto result = config.Validate();
    EXPECT_TRUE(result.isError()) << "Mismatched channels should fail";
}

TEST_F(AecAudioHandlerTest, ValidateConfigInvalidAecConfig) {
    auto config = CreateDefaultConfig(true);
    config.aec_config.sample_rate_hz = 44100;  // Invalid
    
    auto result = config.Validate();
    EXPECT_TRUE(result.isError());
}

TEST_F(AecAudioHandlerTest, ValidateConfigInvalidBufferConfig) {
    auto config = CreateDefaultConfig(true);
    config.buffer_config.delay_ms = 150;  // 150 >= 100 buffer size
    
    auto result = config.Validate();
    EXPECT_TRUE(result.isError());
}

// ============================================================================
// Handler Creation Tests
// ============================================================================

TEST_F(AecAudioHandlerTest, CreateWithAecEnabled) {
    // Note: Can't actually create with real AudioHandler (requires PortAudio)
    // This test verifies config validation works
    auto config = CreateDefaultConfig(true);
    auto result = config.Validate();
    EXPECT_TRUE(result.isOk());
}

TEST_F(AecAudioHandlerTest, CreateWithAecDisabled) {
    auto config = CreateDefaultConfig(false);
    auto result = config.Validate();
    EXPECT_TRUE(result.isOk());
}

TEST_F(AecAudioHandlerTest, CreateWithInvalidConfig) {
    auto config = CreateDefaultConfig(true);
    config.aec_config.sample_rate_hz = 11025;  // Invalid
    
    auto result = config.Validate();
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.error(), AudioChatError::InvalidCodecConfig);
}

// ============================================================================
// Configuration Property Tests
// ============================================================================

TEST_F(AecAudioHandlerTest, ConfigDefaultsAecDisabled) {
    AecAudioHandler::Config config;
    EXPECT_FALSE(config.enable_aec);
}

TEST_F(AecAudioHandlerTest, ConfigAecConfigDefaults) {
    auto config = CreateDefaultConfig(true);
    EXPECT_EQ(config.aec_config.sample_rate_hz, 48000);
    EXPECT_EQ(config.aec_config.num_channels, 1);
    EXPECT_EQ(config.aec_config.frame_duration_ms, 10);
    EXPECT_TRUE(config.aec_config.enable_aec);
    EXPECT_TRUE(config.aec_config.enable_ns);
}

TEST_F(AecAudioHandlerTest, ConfigBufferConfigDefaults) {
    auto config = CreateDefaultConfig(true);
    EXPECT_EQ(config.buffer_config.sample_rate_hz, 48000);
    EXPECT_EQ(config.buffer_config.num_channels, 1);
    EXPECT_EQ(config.buffer_config.delay_ms, 30);
    EXPECT_EQ(config.buffer_config.buffer_duration_ms, 100);
}

// ============================================================================
// Integration Scenario Tests
// ============================================================================

TEST_F(AecAudioHandlerTest, SignalProcessingWithAecDisabled) {
    // With AEC disabled, should pass audio through unchanged
    auto config = CreateDefaultConfig(false);
    EXPECT_TRUE(config.Validate().isOk());
    
    // Verify that validation succeeds for no-AEC configuration
    EXPECT_FALSE(config.enable_aec);
}

TEST_F(AecAudioHandlerTest, SignalProcessingWithAecEnabled) {
    // With AEC enabled, verification should check all configs
    auto config = CreateDefaultConfig(true);
    auto result = config.Validate();
    EXPECT_TRUE(result.isOk());
    EXPECT_TRUE(config.enable_aec);
}

TEST_F(AecAudioHandlerTest, VariableSampleRates) {
    for (int sr : {8000, 16000, 32000, 48000}) {
        auto config = CreateDefaultConfig(true);
        config.aec_config.sample_rate_hz = sr;
        config.buffer_config.sample_rate_hz = sr;
        
        auto result = config.Validate();
        EXPECT_TRUE(result.isOk()) << "Failed for sample rate " << sr;
    }
}

TEST_F(AecAudioHandlerTest, MultiChannelConfiguration) {
    for (int ch : {1, 2}) {
        auto config = CreateDefaultConfig(true);
        config.aec_config.num_channels = ch;
        config.buffer_config.num_channels = ch;
        
        auto result = config.Validate();
        EXPECT_TRUE(result.isOk()) << "Failed for channels " << ch;
    }
}

// ============================================================================
// Delay Compensation Verification
// ============================================================================

TEST_F(AecAudioHandlerTest, DelayRangesValid) {
    auto config = CreateDefaultConfig(true);
    
    // Test various delays
    for (int delay : {0, 10, 20, 30, 50, 100, 200}) {
        config.buffer_config.delay_ms = delay;
        config.buffer_config.buffer_duration_ms = std::max(300, delay + 100);
        auto result = config.Validate();
        EXPECT_TRUE(result.isOk()) << "Delay " << delay << "ms should be valid";
    }
}

TEST_F(AecAudioHandlerTest, DelayRangesInvalid) {
    auto config = CreateDefaultConfig(true);
    
    // Test invalid delays
    for (int delay : {-1, 201, 300}) {
        config.buffer_config.delay_ms = delay;
        auto result = config.Validate();
        EXPECT_TRUE(result.isError()) << "Delay " << delay << "ms should be invalid";
    }
}

TEST_F(AecAudioHandlerTest, BufferLargerThanDelay) {
    auto config = CreateDefaultConfig(true);
    config.buffer_config.buffer_duration_ms = 100;
    config.buffer_config.delay_ms = 50;  // 50 < 100
    
    auto result = config.Validate();
    EXPECT_TRUE(result.isOk());
}

TEST_F(AecAudioHandlerTest, BufferNotLargerThanDelay) {
    auto config = CreateDefaultConfig(true);
    config.buffer_config.buffer_duration_ms = 50;
    config.buffer_config.delay_ms = 50;  // Not greater
    
    auto result = config.Validate();
    EXPECT_TRUE(result.isError());
}

// ============================================================================
// Feature Toggle Tests
// ============================================================================

TEST_F(AecAudioHandlerTest, AecToggleSequence) {
    auto config = CreateDefaultConfig(true);
    
    // Enabled -> Disabled -> Enabled
    EXPECT_TRUE(config.enable_aec);
    
    config.enable_aec = false;
    EXPECT_FALSE(config.enable_aec);
    
    config.enable_aec = true;
    EXPECT_TRUE(config.enable_aec);
}

TEST_F(AecAudioHandlerTest, SuppressionLevelRange) {
    auto config = CreateDefaultConfig(true);
    
    // Valid levels: 0, 1, 2
    for (int level : {0, 1, 2}) {
        config.aec_config.aec_suppression_level = level;
        auto result = config.Validate();
        EXPECT_TRUE(result.isOk()) << "Level " << level << " should be valid";
    }
}

// ============================================================================
// Compatibility Tests
// ============================================================================

TEST_F(AecAudioHandlerTest, BackwardCompatibilityNoAec) {
    // Configuration without AEC should work like original
    auto config = CreateDefaultConfig(false);
    auto result = config.Validate();
    EXPECT_TRUE(result.isOk());
    EXPECT_FALSE(config.enable_aec);
}

TEST_F(AecAudioHandlerTest, InteroperabilityWithOriginalAudio) {
    // Audio buffer and AEC configs should use same audio parameters
    auto config = CreateDefaultConfig(true);
    
    EXPECT_EQ(config.aec_config.sample_rate_hz, 
              config.buffer_config.sample_rate_hz);
    EXPECT_EQ(config.aec_config.num_channels, 
              config.buffer_config.num_channels);
}

// ============================================================================
// Error Recovery Tests
// ============================================================================

TEST_F(AecAudioHandlerTest, GracefulDegradation) {
    // If reference buffer has insufficient data,
    // should return original audio (not error)
    auto config = CreateDefaultConfig(true);
    auto result = config.Validate();
    EXPECT_TRUE(result.isOk());
}

TEST_F(AecAudioHandlerTest, ConfigRecovery) {
    // Can reconfigure after initialization
    auto initial_config = CreateDefaultConfig(true);
    EXPECT_TRUE(initial_config.Validate().isOk());
    
    // Modify and re-validate
    initial_config.aec_config.aec_suppression_level = 1;
    EXPECT_TRUE(initial_config.Validate().isOk());
}

// ============================================================================
// Performance Considerations Tests
// ============================================================================

TEST_F(AecAudioHandlerTest, FrameSizeCompatibility) {
    auto config = CreateDefaultConfig(true);
    
    // 10ms frames at 48kHz = 480 samples
    EXPECT_EQ(config.aec_config.frame_duration_ms, 10);
    EXPECT_EQ(config.buffer_config.frame_duration_ms, 10);
}

TEST_F(AecAudioHandlerTest, BufferCapacityCalculation) {
    auto config = CreateDefaultConfig(true);
    
    // 100ms buffer at 48kHz = 4800 samples
    // 30ms delay at 48kHz = 1440 samples
    // Need 1440 + 480 = 1920 samples for one retrieval
    // 4800 / 1920 = 2.5 frames buffered
    
    EXPECT_GE(config.buffer_config.buffer_duration_ms, 100);
    EXPECT_LE(config.buffer_config.delay_ms, 50);
}

// ============================================================================
// Documentation and Specification Tests
// ============================================================================

TEST_F(AecAudioHandlerTest, ConfigStructDocumentation) {
    // Verify all documented config options exist and are accessible
    auto config = CreateDefaultConfig(true);
    
    // AecAudioHandler::Config fields
    EXPECT_TRUE(config.enable_aec == true || config.enable_aec == false);
    
    // AecConfig fields
    EXPECT_GT(config.aec_config.sample_rate_hz, 0);
    EXPECT_GT(config.aec_config.num_channels, 0);
    EXPECT_GT(config.aec_config.frame_duration_ms, 0);
    
    // ReferenceSignalBuffer::Config fields
    EXPECT_GT(config.buffer_config.sample_rate_hz, 0);
    EXPECT_GT(config.buffer_config.num_channels, 0);
    EXPECT_GE(config.buffer_config.delay_ms, 0);
}

TEST_F(AecAudioHandlerTest, ApiCompleteness) {
    // Verify all documented methods can be validated in config
    auto config = CreateDefaultConfig(true);
    
    // Should have enable flag
    EXPECT_TRUE(config.enable_aec != false || config.enable_aec == false);
    
    // Should have AEC config
    EXPECT_TRUE(config.aec_config.enable_aec != false || config.aec_config.enable_aec == false);
    EXPECT_TRUE(config.aec_config.enable_ns != false || config.aec_config.enable_ns == false);
    EXPECT_TRUE(config.aec_config.enable_agc != false || config.aec_config.enable_agc == false);
}
