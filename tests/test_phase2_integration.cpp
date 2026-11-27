#include <gtest/gtest.h>
#include "AecProcessor.h"
#include "ReferenceSignalBuffer.h"
#include "AecAudioHandler.h"
#include "CodecOptimizer.h"
#include "Logger.h"
#include <cstring>
#include <vector>
#include <chrono>

using namespace AudioChat;

/**
 * @brief Phase 2 Integration Tests
 * 
 * These tests verify the complete AEC pipeline works correctly:
 * 1. AecProcessor configuration
 * 2. ReferenceSignalBuffer with timing
 * 3. AecAudioHandler wrapper integration
 * 4. CodecOptimizer parameter application
 * 5. End-to-end signal flow
 * 6. Performance characteristics
 */
class Phase2IntegrationTest : public ::testing::Test {
protected:
    /**
     * @brief Create test audio data (sine wave approximation)
     */
    std::vector<int16_t> CreateTestAudioFrame(int32_t num_samples, 
                                               int16_t amplitude = 1000) {
        std::vector<int16_t> frame(num_samples);
        for (int32_t i = 0; i < num_samples; ++i) {
            // Simple pseudo-sine for testing
            frame[i] = static_cast<int16_t>(amplitude * (i % 256) / 256);
        }
        return frame;
    }

    /**
     * @brief Create valid AEC configuration for 48kHz
     */
    AecConfig CreateAecConfig() {
        AecConfig config;
        config.sample_rate_hz = 48000;
        config.num_channels = 1;
        config.frame_duration_ms = 10;
        config.enable_aec = true;
        config.enable_ns = true;
        config.enable_agc = true;
        config.aec_suppression_level = 2;
        return config;
    }

    /**
     * @brief Create valid reference buffer configuration
     */
    ReferenceSignalBuffer::Config CreateBufferConfig() {
        ReferenceSignalBuffer::Config config;
        config.sample_rate_hz = 48000;
        config.num_channels = 1;
        config.frame_duration_ms = 10;
        config.delay_ms = 30;  // 30ms delay (playback + acoustic)
        config.buffer_duration_ms = 100;
        return config;
    }

    /**
     * @brief Create valid codec optimization config
     */
    CodecOptimizationConfig CreateCodecConfig() {
        CodecOptimizationConfig config;
        config.bitrate_bps = 24000;
        config.enable_dtx = true;
        config.enable_vbr = true;
        config.enable_fec = true;
        config.packet_loss_percent = 10;
        config.complexity = 5;
        return config;
    }

    /**
     * @brief Measure execution time
     */
    template<typename Func>
    double MeasureExecutionTime(Func func) {
        auto start = std::chrono::high_resolution_clock::now();
        func();
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end - start).count();
    }
};

// ============================================================================
// Component Verification Tests
// ============================================================================

TEST_F(Phase2IntegrationTest, AecProcessorCreation) {
    auto config = CreateAecConfig();
    auto result = AecProcessor::Create(config);
    
    EXPECT_TRUE(result.isOk()) << "AecProcessor should create successfully";
    auto processor = result.value();
    EXPECT_TRUE(processor != nullptr);
    EXPECT_EQ(processor->GetConfig().sample_rate_hz, 48000);
}

TEST_F(Phase2IntegrationTest, ReferenceSignalBufferCreation) {
    auto config = CreateBufferConfig();
    auto result = ReferenceSignalBuffer::Create(config);
    
    EXPECT_TRUE(result.isOk()) << "ReferenceSignalBuffer should create successfully";
    auto buffer = result.value();
    EXPECT_TRUE(buffer != nullptr);
    EXPECT_EQ(buffer->GetConfig().delay_ms, 30);
}

TEST_F(Phase2IntegrationTest, AecProcessorConfiguration) {
    auto config = CreateAecConfig();
    auto result = AecProcessor::Create(config);
    ASSERT_TRUE(result.isOk());
    auto processor = std::move(result.value());

    // Test basic operations
    AudioFrame frame;
    frame.data.resize(480, 100);
    frame.sample_count = 480;
    frame.channels = 1;

    auto ref_result = processor->ProcessReferenceStream(frame);
    EXPECT_TRUE(ref_result.isOk());

    auto cap_result = processor->ProcessCaptureStream(frame);
    EXPECT_TRUE(cap_result.isOk());
}

TEST_F(Phase2IntegrationTest, AecAudioHandlerConfiguration) {
    // Test configuration validation
    AecAudioHandler::Config config;
    config.enable_aec = true;
    config.aec_config = CreateAecConfig();
    config.buffer_config = CreateBufferConfig();

    auto result = config.Validate();
    EXPECT_TRUE(result.isOk()) << "AecAudioHandler config should validate";
}

TEST_F(Phase2IntegrationTest, CodecOptimizerConfiguration) {
    auto config = CreateCodecConfig();
    auto result = config.Validate();
    
    EXPECT_TRUE(result.isOk()) << "CodecOptimizer config should validate";
}

// ============================================================================
// Signal Flow Tests
// ============================================================================

TEST_F(Phase2IntegrationTest, ReferenceSignalFlow) {
    auto config = CreateBufferConfig();
    auto result = ReferenceSignalBuffer::Create(config);
    ASSERT_TRUE(result.isOk());
    auto buffer = std::move(result.value());

    // Push 5 frames to build up delay buffer
    for (int i = 0; i < 5; ++i) {
        auto frame = CreateTestAudioFrame(480);
        auto push_result = buffer->PushReferenceSignal(frame.data(), 480);
        EXPECT_TRUE(push_result.isOk());
    }

    // Should now have enough data to retrieve with delay
    auto retrieve_result = buffer->GetDelayedReference(480);
    EXPECT_TRUE(retrieve_result.isOk()) << "Should have sufficient buffered data";
}

TEST_F(Phase2IntegrationTest, CaptureProcessingFlow) {
    auto aec_config = CreateAecConfig();
    auto aec_result = AecProcessor::Create(aec_config);
    ASSERT_TRUE(aec_result.isOk());
    auto processor = std::move(aec_result.value());

    // Create test capture frame
    auto capture_frame = CreateTestAudioFrame(480);
    AudioFrame frame;
    frame.data = capture_frame;
    frame.sample_count = 480;
    frame.channels = 1;

    // Process through AEC
    auto process_result = processor->ProcessCaptureStream(frame);
    EXPECT_TRUE(process_result.isOk());

    if (process_result.isOk()) {
        auto output = process_result.value();
        EXPECT_EQ(output.sample_count, 480);
        EXPECT_EQ(output.channels, 1);
    }
}

// ============================================================================
// End-to-End Pipeline Tests
// ============================================================================

TEST_F(Phase2IntegrationTest, CompleteAecPipeline) {
    // 1. Create all components
    auto aec_config = CreateAecConfig();
    auto buffer_config = CreateBufferConfig();

    auto aec_result = AecProcessor::Create(aec_config);
    ASSERT_TRUE(aec_result.isOk());
    auto processor = std::move(aec_result.value());

    auto buf_result = ReferenceSignalBuffer::Create(buffer_config);
    ASSERT_TRUE(buf_result.isOk());
    auto buffer = std::move(buf_result.value());

    // 2. Simulate reference signal (speaker output)
    for (int i = 0; i < 5; ++i) {
        auto ref_frame = CreateTestAudioFrame(480, 500 + i * 100);
        buffer->PushReferenceSignal(ref_frame.data(), 480);
    }

    // 3. Process capture signal (microphone input)
    auto capture_frame = CreateTestAudioFrame(480, 2000);
    AudioFrame cap;
    cap.data = capture_frame;
    cap.sample_count = 480;
    cap.channels = 1;

    if (buffer->HasSufficientData(480)) {
        auto ref = buffer->GetDelayedReference(480);
        ASSERT_TRUE(ref.isOk());

        // Process reference through AEC
        AudioFrame ref_frame;
        ref_frame.data = ref.value().data;
        ref_frame.sample_count = ref.value().num_samples;
        ref_frame.channels = 1;

        auto ref_process = processor->ProcessReferenceStream(ref_frame);
        EXPECT_TRUE(ref_process.isOk());

        // Process capture through AEC
        auto cap_process = processor->ProcessCaptureStream(cap);
        EXPECT_TRUE(cap_process.isOk());
    }
}

TEST_F(Phase2IntegrationTest, MultiframeProcessing) {
    auto aec_config = CreateAecConfig();
    auto buffer_config = CreateBufferConfig();

    auto aec_result = AecProcessor::Create(aec_config);
    ASSERT_TRUE(aec_result.isOk());
    auto processor = std::move(aec_result.value());

    auto buf_result = ReferenceSignalBuffer::Create(buffer_config);
    ASSERT_TRUE(buf_result.isOk());
    auto buffer = std::move(buf_result.value());

    // Process 100 frames (1 second at 48kHz with 10ms frames)
    for (int frame_idx = 0; frame_idx < 100; ++frame_idx) {
        // Reference (speaker) signal
        auto ref_data = CreateTestAudioFrame(480, 1000);
        auto ref_push = buffer->PushReferenceSignal(ref_data.data(), 480);
        EXPECT_TRUE(ref_push.isOk());

        // Capture (microphone) signal
        auto cap_data = CreateTestAudioFrame(480, 500 + frame_idx);
        AudioFrame cap_frame;
        cap_frame.data = cap_data;
        cap_frame.sample_count = 480;
        cap_frame.channels = 1;

        auto cap_process = processor->ProcessCaptureStream(cap_frame);
        EXPECT_TRUE(cap_process.isOk());
    }

    // Verify statistics show frames were processed
    std::string stats = processor->GetStatistics();
    EXPECT_FALSE(stats.empty());
    EXPECT_TRUE(stats.find("AecProcessor Statistics") != std::string::npos);
}

// ============================================================================
// Configuration Consistency Tests
// ============================================================================

TEST_F(Phase2IntegrationTest, AecAndBufferConfigConsistency) {
    // Configs must have matching sample rates and channels
    auto aec_config = CreateAecConfig();
    auto buffer_config = CreateBufferConfig();

    EXPECT_EQ(aec_config.sample_rate_hz, buffer_config.sample_rate_hz);
    EXPECT_EQ(aec_config.num_channels, buffer_config.num_channels);
}

TEST_F(Phase2IntegrationTest, AudioFrameConsistency) {
    auto config = CreateAecConfig();
    auto result = AecProcessor::Create(config);
    ASSERT_TRUE(result.isOk());
    auto processor = std::move(result.value());

    // Frame at 48kHz, 10ms duration, 1 channel
    // Should have 480 samples
    AudioFrame frame;
    frame.data.resize(480);
    frame.sample_count = 480;
    frame.channels = 1;

    auto process_result = processor->ProcessCaptureStream(frame);
    EXPECT_TRUE(process_result.isOk());

    if (process_result.isOk()) {
        auto output = process_result.value();
        EXPECT_EQ(output.sample_count, 480);
    }
}

// ============================================================================
// Performance Tests
// ============================================================================

TEST_F(Phase2IntegrationTest, AecProcessingLatency) {
    auto config = CreateAecConfig();
    auto result = AecProcessor::Create(config);
    ASSERT_TRUE(result.isOk());
    auto processor = std::move(result.value());

    AudioFrame frame;
    frame.data.resize(480, 100);
    frame.sample_count = 480;
    frame.channels = 1;

    // Measure single frame processing time
    double exec_time = MeasureExecutionTime([&]() {
        processor->ProcessCaptureStream(frame);
    });

    // Should be very fast (< 5ms for stub implementation)
    EXPECT_LT(exec_time, 5.0) << "AEC processing should be fast";
    LOG_INFO("AEC processing latency: {:.3f}ms", exec_time);
}

TEST_F(Phase2IntegrationTest, BufferOperationLatency) {
    auto config = CreateBufferConfig();
    auto result = ReferenceSignalBuffer::Create(config);
    ASSERT_TRUE(result.isOk());
    auto buffer = std::move(result.value());

    auto frame = CreateTestAudioFrame(480);

    // Fill buffer first
    for (int i = 0; i < 5; ++i) {
        buffer->PushReferenceSignal(frame.data(), 480);
    }

    // Measure push latency
    double push_time = MeasureExecutionTime([&]() {
        buffer->PushReferenceSignal(frame.data(), 480);
    });

    // Measure retrieve latency
    double retrieve_time = MeasureExecutionTime([&]() {
        buffer->GetDelayedReference(480);
    });

    EXPECT_LT(push_time, 1.0) << "Buffer push should be fast";
    EXPECT_LT(retrieve_time, 1.0) << "Buffer retrieve should be fast";

    LOG_INFO("Buffer push latency: {:.3f}ms", push_time);
    LOG_INFO("Buffer retrieve latency: {:.3f}ms", retrieve_time);
}

// ============================================================================
// Error Recovery Tests
// ============================================================================

TEST_F(Phase2IntegrationTest, GracefulDegradationWithUnderflow) {
    auto aec_config = CreateAecConfig();
    auto buffer_config = CreateBufferConfig();

    auto aec_result = AecProcessor::Create(aec_config);
    ASSERT_TRUE(aec_result.isOk());
    auto processor = std::move(aec_result.value());

    auto buf_result = ReferenceSignalBuffer::Create(buffer_config);
    ASSERT_TRUE(buf_result.isOk());
    auto buffer = std::move(buf_result.value());

    // Try to process with insufficient buffer (should fail gracefully)
    buffer->PushReferenceSignal(CreateTestAudioFrame(480).data(), 480);

    // Should not have enough for delay + data
    EXPECT_FALSE(buffer->HasSufficientData(480));
}

TEST_F(Phase2IntegrationTest, ConfigValidationFailure) {
    AecConfig config;
    config.sample_rate_hz = 44100;  // Invalid

    auto result = AecProcessor::Create(config);
    EXPECT_TRUE(result.isError()) << "Should reject invalid config";
    EXPECT_EQ(result.error(), AudioChatError::InvalidCodecConfig);
}

TEST_F(Phase2IntegrationTest, ResetAndResync) {
    auto config = CreateBufferConfig();
    auto result = ReferenceSignalBuffer::Create(config);
    ASSERT_TRUE(result.isOk());
    auto buffer = std::move(result.value());

    // Add some data
    for (int i = 0; i < 5; ++i) {
        buffer->PushReferenceSignal(CreateTestAudioFrame(480).data(), 480);
    }

    int32_t buffered_before = buffer->GetBufferedSamples();
    EXPECT_GT(buffered_before, 0);

    // Clear/reset
    buffer->Clear();

    int32_t buffered_after = buffer->GetBufferedSamples();
    EXPECT_EQ(buffered_after, 0);
}

// ============================================================================
// Documentation Verification Tests
// ============================================================================

TEST_F(Phase2IntegrationTest, StatisticsAvailable) {
    auto aec_config = CreateAecConfig();
    auto result = AecProcessor::Create(aec_config);
    ASSERT_TRUE(result.isOk());
    auto processor = std::move(result.value());

    // Process some frames
    AudioFrame frame;
    frame.data.resize(480, 100);
    frame.sample_count = 480;
    frame.channels = 1;

    processor->ProcessCaptureStream(frame);
    processor->ProcessCaptureStream(frame);

    std::string stats = processor->GetStatistics();
    EXPECT_FALSE(stats.empty());
    EXPECT_TRUE(stats.find("Sample rate") != std::string::npos);
    EXPECT_TRUE(stats.find("Frames processed") != std::string::npos);
}

TEST_F(Phase2IntegrationTest, ApiCompleteness) {
    auto aec_config = CreateAecConfig();
    auto result = AecProcessor::Create(aec_config);
    ASSERT_TRUE(result.isOk());
    auto processor = std::move(result.value());

    // Verify all public methods exist and are callable
    EXPECT_NO_THROW({
        processor->SetAecEnabled(true);
        processor->SetAecSuppressionLevel(2);
        processor->GetConfig();
        processor->GetStatistics();
    });
}

// ============================================================================
// Scenario Tests (Real-world usage patterns)
// ============================================================================

TEST_F(Phase2IntegrationTest, ScenarioQuietCall) {
    // Scenario: Quiet call with minimal background noise
    auto aec_config = CreateAecConfig();
    aec_config.enable_ns = true;      // Noise suppression
    aec_config.enable_agc = true;     // Auto gain
    aec_config.aec_suppression_level = 1;  // Moderate suppression

    auto result = AecProcessor::Create(aec_config);
    ASSERT_TRUE(result.isOk());

    // Should succeed
    EXPECT_TRUE(result.isOk());
}

TEST_F(Phase2IntegrationTest, ScenarioNoisyEnvironment) {
    // Scenario: Noisy environment (cafe, office)
    auto aec_config = CreateAecConfig();
    aec_config.enable_ns = true;      // Strong noise suppression
    aec_config.enable_agc = true;
    aec_config.aec_suppression_level = 2;  // High suppression

    auto result = AecProcessor::Create(aec_config);
    ASSERT_TRUE(result.isOk());

    // Should succeed
    EXPECT_TRUE(result.isOk());
}

TEST_F(Phase2IntegrationTest, ScenarioLowBandwidth) {
    // Scenario: Low bandwidth network
    auto codec_config = CreateCodecConfig();
    codec_config.bitrate_bps = 12000;      // Low bitrate
    codec_config.enable_dtx = true;        // Critical for low BW
    codec_config.enable_vbr = true;        // VBR helps
    codec_config.complexity = 3;           // Lower CPU

    auto result = codec_config.Validate();
    EXPECT_TRUE(result.isOk());
}

} // namespace (empty, tests are in global namespace for gtest)
