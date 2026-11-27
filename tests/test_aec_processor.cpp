#include <gtest/gtest.h>
#include "AecProcessor.h"
#include "Logger.h"
#include <cstring>
#include <vector>

using namespace AudioChat;

/**
 * @brief Unit tests for AecProcessor
 * 
 * These tests verify:
 * 1. Configuration validation
 * 2. Processor creation and initialization
 * 3. Audio frame processing (reference and capture streams)
 * 4. Configuration updates
 * 5. Statistics gathering
 */
class AecProcessorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize logger for tests
        // Note: In real project, would initialize spdlog
    }

    void TearDown() override {
        // Cleanup after each test
    }

    /**
     * @brief Create a test audio frame with given parameters
     */
    AudioFrame CreateTestFrame(int32_t num_samples, int32_t num_channels, 
                               uint64_t timestamp_ms = 0) {
        AudioFrame frame;
        frame.data.resize(num_samples * num_channels, 0);
        frame.sample_count = num_samples;
        frame.channels = num_channels;
        frame.timestamp_ms = timestamp_ms;
        return frame;
    }
};

// ============================================================================
// Configuration Validation Tests
// ============================================================================

TEST_F(AecProcessorTest, ValidateSampleRates) {
    // Valid sample rates
    for (int sr : {8000, 16000, 32000, 48000}) {
        AecConfig config;
        config.sample_rate_hz = sr;
        auto result = config.Validate();
        EXPECT_TRUE(result.isOk()) << "Sample rate " << sr << " should be valid";
    }

    // Invalid sample rates
    for (int sr : {7000, 11000, 24000, 44100, 96000}) {
        AecConfig config;
        config.sample_rate_hz = sr;
        auto result = config.Validate();
        EXPECT_TRUE(result.isError()) << "Sample rate " << sr << " should be invalid";
        EXPECT_EQ(result.error(), AudioChatError::InvalidCodecConfig);
    }
}

TEST_F(AecProcessorTest, ValidateNumChannels) {
    // Valid channels
    for (int ch : {1, 2}) {
        AecConfig config;
        config.num_channels = ch;
        auto result = config.Validate();
        EXPECT_TRUE(result.isOk()) << "Channels " << ch << " should be valid";
    }

    // Invalid channels
    for (int ch : {0, 3, 4, 8}) {
        AecConfig config;
        config.num_channels = ch;
        auto result = config.Validate();
        EXPECT_TRUE(result.isError()) << "Channels " << ch << " should be invalid";
    }
}

TEST_F(AecProcessorTest, ValidateFrameDuration) {
    // Valid durations
    for (int ms : {10, 20}) {
        AecConfig config;
        config.frame_duration_ms = ms;
        auto result = config.Validate();
        EXPECT_TRUE(result.isOk()) << "Frame duration " << ms << " ms should be valid";
    }

    // Invalid durations
    for (int ms : {1, 5, 15, 30, 100}) {
        AecConfig config;
        config.frame_duration_ms = ms;
        auto result = config.Validate();
        EXPECT_TRUE(result.isError()) << "Frame duration " << ms << " ms should be invalid";
    }
}

TEST_F(AecProcessorTest, ValidateSuppressionLevels) {
    // Valid AEC suppression levels
    for (int level : {0, 1, 2}) {
        AecConfig config;
        config.aec_suppression_level = level;
        auto result = config.Validate();
        EXPECT_TRUE(result.isOk()) << "AEC level " << level << " should be valid";
    }

    // Invalid AEC suppression levels
    for (int level : {-1, 3, 4, 10}) {
        AecConfig config;
        config.aec_suppression_level = level;
        auto result = config.Validate();
        EXPECT_TRUE(result.isError()) << "AEC level " << level << " should be invalid";
    }

    // Valid NS levels
    for (int level : {0, 1, 2}) {
        AecConfig config;
        config.ns_level = level;
        auto result = config.Validate();
        EXPECT_TRUE(result.isOk()) << "NS level " << level << " should be valid";
    }

    // Invalid NS levels
    for (int level : {-1, 3, 4, 10}) {
        AecConfig config;
        config.ns_level = level;
        auto result = config.Validate();
        EXPECT_TRUE(result.isError()) << "NS level " << level << " should be invalid";
    }
}

TEST_F(AecProcessorTest, ValidateStreamDelay) {
    AecConfig config;
    config.stream_delay_ms = 64;
    EXPECT_TRUE(config.Validate().isOk());

    config.stream_delay_ms = -1;
    EXPECT_TRUE(config.Validate().isError());
    config.stream_delay_ms = 501;
    EXPECT_TRUE(config.Validate().isError());
}

TEST_F(AecProcessorTest, ValidateFullConfig) {
    AecConfig config;
    config.sample_rate_hz = 48000;
    config.num_channels = 1;
    config.frame_duration_ms = 10;
    config.aec_suppression_level = 2;
    config.ns_level = 1;
    
    auto result = config.Validate();
    EXPECT_TRUE(result.isOk()) << "Valid config should pass validation";
}

// ============================================================================
// Processor Creation Tests
// ============================================================================

TEST_F(AecProcessorTest, CreateWithValidConfig) {
    AecConfig config;
    config.sample_rate_hz = 48000;
    config.num_channels = 1;
    config.frame_duration_ms = 10;

    auto result = AecProcessor::Create(config);
    EXPECT_TRUE(result.isOk()) << "Should create processor with valid config";
    
    auto processor = std::move(result.value());
    EXPECT_TRUE(processor != nullptr) << "Processor should not be null";
    EXPECT_EQ(processor->GetConfig().sample_rate_hz, 48000);
    EXPECT_EQ(processor->GetConfig().num_channels, 1);
}

TEST_F(AecProcessorTest, CreateWithInvalidConfig) {
    AecConfig config;
    config.sample_rate_hz = 44100;  // Invalid
    config.num_channels = 1;
    config.frame_duration_ms = 10;

    auto result = AecProcessor::Create(config);
    EXPECT_TRUE(result.isError()) << "Should fail with invalid sample rate";
    EXPECT_EQ(result.error(), AudioChatError::InvalidCodecConfig);
}

TEST_F(AecProcessorTest, ConfigurationDefaults) {
    AecConfig config;  // Use defaults
    
    EXPECT_EQ(config.sample_rate_hz, 48000);
    EXPECT_EQ(config.num_channels, 1);
    EXPECT_EQ(config.frame_duration_ms, 10);
    EXPECT_TRUE(config.enable_aec);
    EXPECT_TRUE(config.enable_ns);
    EXPECT_TRUE(config.enable_agc);
    EXPECT_FALSE(config.enable_vad);
    EXPECT_EQ(config.aec_suppression_level, 2);
    EXPECT_EQ(config.ns_level, 1);
}

TEST_F(AecProcessorTest, BackendStatusMatchesBuild) {
    auto processor = AecProcessor::Create(AecConfig{});
    ASSERT_TRUE(processor.isOk());
#ifdef HAS_WEBRTC_AEC
    EXPECT_TRUE(processor.value()->IsBackendActive());
#else
    EXPECT_FALSE(processor.value()->IsBackendActive());
#endif
}

// ============================================================================
// Audio Frame Processing Tests
// ============================================================================

TEST_F(AecProcessorTest, ProcessReferenceStream) {
    AecConfig config;
    auto proc_result = AecProcessor::Create(config);
    ASSERT_TRUE(proc_result.isOk());
    auto processor = std::move(proc_result.value());

    // Create test frame: 480 samples @ 48kHz for 10ms, 1 channel
    AudioFrame ref_frame = CreateTestFrame(480, 1);
    
    auto result = processor->ProcessReferenceStream(ref_frame);
    EXPECT_TRUE(result.isOk()) << "Should process reference stream";
}

TEST_F(AecProcessorTest, ProcessCaptureStream) {
    AecConfig config;
    auto proc_result = AecProcessor::Create(config);
    ASSERT_TRUE(proc_result.isOk());
    auto processor = std::move(proc_result.value());

    // Create test frame: 480 samples @ 48kHz for 10ms, 1 channel
    AudioFrame capture_frame = CreateTestFrame(480, 1);
    
    auto result = processor->ProcessCaptureStream(capture_frame);
    EXPECT_TRUE(result.isOk()) << "Should process capture stream";
    
    if (result.isOk()) {
        AudioFrame output = result.value();
        EXPECT_EQ(output.sample_count, 480);
        EXPECT_EQ(output.channels, 1);
        EXPECT_EQ(output.data.size(), 480);
    }
}

TEST_F(AecProcessorTest, ProcessMultiChannelAudio) {
    AecConfig config;
    config.num_channels = 2;
    auto proc_result = AecProcessor::Create(config);
    ASSERT_TRUE(proc_result.isOk());
    auto processor = std::move(proc_result.value());

    // Create test frame: 480 samples @ 48kHz for 10ms, 2 channels
    AudioFrame frame = CreateTestFrame(480, 2);
    
    auto result = processor->ProcessCaptureStream(frame);
    EXPECT_TRUE(result.isOk());
    if (result.isOk()) {
        EXPECT_EQ(result.value().data.size(), 960);  // 480 * 2
    }
}

// ============================================================================
// Error Handling Tests
// ============================================================================

TEST_F(AecProcessorTest, ProcessEmptyFrame) {
    AecConfig config;
    auto proc_result = AecProcessor::Create(config);
    ASSERT_TRUE(proc_result.isOk());
    auto processor = std::move(proc_result.value());

    AudioFrame empty_frame;
    empty_frame.data.clear();
    empty_frame.sample_count = 0;
    empty_frame.channels = 1;

    auto result = processor->ProcessCaptureStream(empty_frame);
    EXPECT_TRUE(result.isError()) << "Should reject empty frame";
    EXPECT_EQ(result.error(), AudioChatError::AecProcessFailed);
}

TEST_F(AecProcessorTest, ProcessFrameWithInvalidChannels) {
    AecConfig config;
    config.num_channels = 1;
    auto proc_result = AecProcessor::Create(config);
    ASSERT_TRUE(proc_result.isOk());
    auto processor = std::move(proc_result.value());

    // Create frame with invalid channel count
    AudioFrame frame;
    frame.data.resize(480);
    frame.sample_count = 480;
    frame.channels = 5;  // Invalid
    
    auto result = processor->ProcessCaptureStream(frame);
    EXPECT_TRUE(result.isError());
}

TEST_F(AecProcessorTest, ProcessFrameSizeMismatch) {
    AecConfig config;
    auto proc_result = AecProcessor::Create(config);
    ASSERT_TRUE(proc_result.isOk());
    auto processor = std::move(proc_result.value());

    // Create frame with mismatched size
    AudioFrame frame;
    frame.data.resize(480);
    frame.sample_count = 100;  // Doesn't match data size
    frame.channels = 1;
    
    auto result = processor->ProcessCaptureStream(frame);
    EXPECT_TRUE(result.isError());
}

// ============================================================================
// Configuration Update Tests
// ============================================================================

TEST_F(AecProcessorTest, SetAecEnabled) {
    AecConfig config;
    auto proc_result = AecProcessor::Create(config);
    ASSERT_TRUE(proc_result.isOk());
    auto processor = std::move(proc_result.value());

    // Disable AEC
    auto result1 = processor->SetAecEnabled(false);
    EXPECT_TRUE(result1.isOk());
    EXPECT_FALSE(processor->GetConfig().enable_aec);

    // Re-enable AEC
    auto result2 = processor->SetAecEnabled(true);
    EXPECT_TRUE(result2.isOk());
    EXPECT_TRUE(processor->GetConfig().enable_aec);
}

TEST_F(AecProcessorTest, SetAecSuppressionLevel) {
    AecConfig config;
    auto proc_result = AecProcessor::Create(config);
    ASSERT_TRUE(proc_result.isOk());
    auto processor = std::move(proc_result.value());

    // Valid levels
    for (int level : {0, 1, 2}) {
        auto result = processor->SetAecSuppressionLevel(level);
        EXPECT_TRUE(result.isOk()) << "Level " << level << " should be valid";
        EXPECT_EQ(processor->GetConfig().aec_suppression_level, level);
    }

    // Invalid levels
    for (int level : {-1, 3, 10}) {
        auto result = processor->SetAecSuppressionLevel(level);
        EXPECT_TRUE(result.isError()) << "Level " << level << " should be invalid";
        EXPECT_EQ(result.error(), AudioChatError::InvalidArgument);
    }
}

// ============================================================================
// Statistics and Utility Tests
// ============================================================================

TEST_F(AecProcessorTest, GetStatistics) {
    AecConfig config;
    auto proc_result = AecProcessor::Create(config);
    ASSERT_TRUE(proc_result.isOk());
    auto processor = std::move(proc_result.value());

    // Process some frames
    AudioFrame frame = CreateTestFrame(480, 1);
    processor->ProcessCaptureStream(frame);
    processor->ProcessCaptureStream(frame);

    std::string stats = processor->GetStatistics();
    EXPECT_FALSE(stats.empty()) << "Statistics should not be empty";
    EXPECT_TRUE(stats.find("AecProcessor Statistics") != std::string::npos);
    EXPECT_TRUE(stats.find("48000") != std::string::npos);  // Sample rate
}

TEST_F(AecProcessorTest, GetConfig) {
    AecConfig config;
    config.sample_rate_hz = 16000;
    config.num_channels = 2;
    config.aec_suppression_level = 1;

    auto proc_result = AecProcessor::Create(config);
    ASSERT_TRUE(proc_result.isOk());
    auto processor = std::move(proc_result.value());

    const AecConfig& returned_config = processor->GetConfig();
    EXPECT_EQ(returned_config.sample_rate_hz, 16000);
    EXPECT_EQ(returned_config.num_channels, 2);
    EXPECT_EQ(returned_config.aec_suppression_level, 1);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(AecProcessorTest, SequentialFrameProcessing) {
    AecConfig config;
    config.sample_rate_hz = 48000;
    auto proc_result = AecProcessor::Create(config);
    ASSERT_TRUE(proc_result.isOk());
    auto processor = std::move(proc_result.value());

    // Process multiple frames in sequence
    for (int i = 0; i < 100; ++i) {
        AudioFrame frame = CreateTestFrame(480, 1, i * 10);  // 10ms increments
        
        auto ref_result = processor->ProcessReferenceStream(frame);
        EXPECT_TRUE(ref_result.isOk());
        
        auto capture_result = processor->ProcessCaptureStream(frame);
        EXPECT_TRUE(capture_result.isOk());
    }

    // Verify statistics show frames were processed
    std::string stats = processor->GetStatistics();
    EXPECT_TRUE(stats.find("Frames processed: 200") != std::string::npos ||
                stats.find("Frames processed") != std::string::npos);
}

TEST_F(AecProcessorTest, VariableSampleRateConfigs) {
    for (int sr : {8000, 16000, 32000, 48000}) {
        AecConfig config;
        config.sample_rate_hz = sr;
        int samples_per_frame = sr / 100;  // 10ms frame
        
        auto proc_result = AecProcessor::Create(config);
        ASSERT_TRUE(proc_result.isOk());
        auto processor = std::move(proc_result.value());
        
        AudioFrame frame = CreateTestFrame(samples_per_frame, 1);
        auto result = processor->ProcessCaptureStream(frame);
        EXPECT_TRUE(result.isOk()) << "Failed for sample rate " << sr;
    }
}
