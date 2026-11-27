#include <gtest/gtest.h>
#include "ReferenceSignalBuffer.h"
#include <cstring>
#include <algorithm>
#include <vector>

using namespace AudioChat;

/**
 * @brief Unit tests for ReferenceSignalBuffer
 * 
 * Tests verify:
 * 1. Configuration validation
 * 2. Buffer creation and initialization
 * 3. Pushing reference signals
 * 4. Retrieving delayed reference signals
 * 5. Delay compensation accuracy
 * 6. Buffer overflow/underflow handling
 * 7. Statistics gathering
 */
class ReferenceSignalBufferTest : public ::testing::Test {
protected:
    /**
     * @brief Create test audio data
     */
    std::vector<int16_t> CreateTestData(int32_t num_samples, int32_t num_channels, 
                                        int16_t value = 100) {
        std::vector<int16_t> data(num_samples * num_channels, value);
        return data;
    }

    /**
     * @brief Create default test config
     */
    ReferenceSignalBuffer::Config CreateTestConfig() {
        ReferenceSignalBuffer::Config config;
        config.sample_rate_hz = 48000;
        config.num_channels = 1;
        config.frame_duration_ms = 10;
        config.delay_ms = 30;
        config.buffer_duration_ms = 100;
        return config;
    }
};

// ============================================================================
// Configuration Validation Tests
// ============================================================================

TEST_F(ReferenceSignalBufferTest, ValidateSampleRates) {
    // Valid sample rates
    for (int sr : {8000, 16000, 32000, 48000}) {
        ReferenceSignalBuffer::Config config = CreateTestConfig();
        config.sample_rate_hz = sr;
        auto result = config.Validate();
        EXPECT_TRUE(result.isOk()) << "Sample rate " << sr << " should be valid";
    }

    // Invalid sample rates
    for (int sr : {7000, 44100, 96000}) {
        ReferenceSignalBuffer::Config config = CreateTestConfig();
        config.sample_rate_hz = sr;
        auto result = config.Validate();
        EXPECT_TRUE(result.isError()) << "Sample rate " << sr << " should be invalid";
    }
}

TEST_F(ReferenceSignalBufferTest, ValidateNumChannels) {
    // Valid channels
    for (int ch : {1, 2}) {
        ReferenceSignalBuffer::Config config = CreateTestConfig();
        config.num_channels = ch;
        auto result = config.Validate();
        EXPECT_TRUE(result.isOk()) << "Channels " << ch << " should be valid";
    }

    // Invalid channels
    for (int ch : {0, 3, 8}) {
        ReferenceSignalBuffer::Config config = CreateTestConfig();
        config.num_channels = ch;
        auto result = config.Validate();
        EXPECT_TRUE(result.isError()) << "Channels " << ch << " should be invalid";
    }
}

TEST_F(ReferenceSignalBufferTest, ValidateFrameDuration) {
    // Valid durations
    for (int ms : {10, 20}) {
        ReferenceSignalBuffer::Config config = CreateTestConfig();
        config.frame_duration_ms = ms;
        auto result = config.Validate();
        EXPECT_TRUE(result.isOk()) << "Frame duration " << ms << " ms should be valid";
    }

    // Invalid durations
    for (int ms : {1, 5, 15, 30}) {
        ReferenceSignalBuffer::Config config = CreateTestConfig();
        config.frame_duration_ms = ms;
        auto result = config.Validate();
        EXPECT_TRUE(result.isError()) << "Frame duration " << ms << " ms should be invalid";
    }
}

TEST_F(ReferenceSignalBufferTest, ValidateDelay) {
    ReferenceSignalBuffer::Config config = CreateTestConfig();
    
    // Valid delays: 0-200ms
    for (int delay : {0, 1, 30, 100, 200}) {
        config.delay_ms = delay;
        config.buffer_duration_ms = std::max(300, delay + 100);
        auto result = config.Validate();
        EXPECT_TRUE(result.isOk()) << "Delay " << delay << " ms should be valid";
    }

    // Invalid delays
    for (int delay : {-1, 201, 300}) {
        config.delay_ms = delay;
        auto result = config.Validate();
        EXPECT_TRUE(result.isError()) << "Delay " << delay << " ms should be invalid";
    }
}

TEST_F(ReferenceSignalBufferTest, ValidateBufferDuration) {
    ReferenceSignalBuffer::Config config = CreateTestConfig();
    
    // Valid buffer durations: 100-500ms
    for (int buf : {100, 150, 300, 500}) {
        config.buffer_duration_ms = buf;
        auto result = config.Validate();
        EXPECT_TRUE(result.isOk()) << "Buffer duration " << buf << " ms should be valid";
    }

    // Invalid buffer durations
    for (int buf : {50, 99, 501}) {
        config.buffer_duration_ms = buf;
        auto result = config.Validate();
        EXPECT_TRUE(result.isError()) << "Buffer duration " << buf << " ms should be invalid";
    }
}

TEST_F(ReferenceSignalBufferTest, ValidateBufferGreaterThanDelay) {
    ReferenceSignalBuffer::Config config = CreateTestConfig();
    
    // Buffer must be > delay
    config.buffer_duration_ms = 50;
    config.delay_ms = 50;  // Equal - should fail
    auto result = config.Validate();
    EXPECT_TRUE(result.isError()) << "Buffer must be > delay";

    // Buffer > delay - should pass
    config.buffer_duration_ms = 100;
    config.delay_ms = 50;
    result = config.Validate();
    EXPECT_TRUE(result.isOk());
}

// ============================================================================
// Buffer Creation Tests
// ============================================================================

TEST_F(ReferenceSignalBufferTest, CreateWithValidConfig) {
    auto config = CreateTestConfig();
    auto result = ReferenceSignalBuffer::Create(config);
    EXPECT_TRUE(result.isOk()) << "Should create buffer with valid config";
    
    auto buffer = std::move(result.value());
    EXPECT_TRUE(buffer != nullptr);
}

TEST_F(ReferenceSignalBufferTest, CreateWithInvalidConfig) {
    auto config = CreateTestConfig();
    config.sample_rate_hz = 44100;  // Invalid
    auto result = ReferenceSignalBuffer::Create(config);
    EXPECT_TRUE(result.isError()) << "Should fail with invalid config";
}

TEST_F(ReferenceSignalBufferTest, ConfigurationDefaults) {
    ReferenceSignalBuffer::Config config;
    
    EXPECT_EQ(config.sample_rate_hz, 48000);
    EXPECT_EQ(config.num_channels, 1);
    EXPECT_EQ(config.frame_duration_ms, 10);
    EXPECT_EQ(config.delay_ms, 30);
    EXPECT_EQ(config.buffer_duration_ms, 100);
}

// ============================================================================
// Push Reference Signal Tests
// ============================================================================

TEST_F(ReferenceSignalBufferTest, PushReferenceSignal) {
    auto config = CreateTestConfig();
    auto buf_result = ReferenceSignalBuffer::Create(config);
    ASSERT_TRUE(buf_result.isOk());
    auto buffer = std::move(buf_result.value());

    // Push 10ms of reference signal (480 samples @ 48kHz)
    auto data = CreateTestData(480, 1, 100);
    auto result = buffer->PushReferenceSignal(data.data(), 480);
    
    EXPECT_TRUE(result.isOk()) << "Should push reference signal";
    EXPECT_EQ(buffer->GetBufferedSamples(), 480);
}

TEST_F(ReferenceSignalBufferTest, PushMultipleFrames) {
    auto config = CreateTestConfig();
    auto buf_result = ReferenceSignalBuffer::Create(config);
    ASSERT_TRUE(buf_result.isOk());
    auto buffer = std::move(buf_result.value());

    // Push 10 frames
    for (int i = 0; i < 10; ++i) {
        auto data = CreateTestData(480, 1, 100 + i);
        auto result = buffer->PushReferenceSignal(data.data(), 480);
        EXPECT_TRUE(result.isOk());
    }

    EXPECT_EQ(buffer->GetBufferedSamples(), 4800);  // 480 * 10
}

TEST_F(ReferenceSignalBufferTest, PushMultiChannelAudio) {
    auto config = CreateTestConfig();
    config.num_channels = 2;
    auto buf_result = ReferenceSignalBuffer::Create(config);
    ASSERT_TRUE(buf_result.isOk());
    auto buffer = std::move(buf_result.value());

    // Push 2-channel audio (480 samples per channel)
    auto data = CreateTestData(480, 2);
    auto result = buffer->PushReferenceSignal(data.data(), 480);
    
    EXPECT_TRUE(result.isOk());
    EXPECT_EQ(buffer->GetBufferedSamples(), 480);  // 480 samples per channel
}

TEST_F(ReferenceSignalBufferTest, PushNullData) {
    auto config = CreateTestConfig();
    auto buf_result = ReferenceSignalBuffer::Create(config);
    ASSERT_TRUE(buf_result.isOk());
    auto buffer = std::move(buf_result.value());

    auto result = buffer->PushReferenceSignal(nullptr, 480);
    EXPECT_TRUE(result.isError());
}

TEST_F(ReferenceSignalBufferTest, PushInvalidSampleCount) {
    auto config = CreateTestConfig();
    auto buf_result = ReferenceSignalBuffer::Create(config);
    ASSERT_TRUE(buf_result.isOk());
    auto buffer = std::move(buf_result.value());

    auto data = CreateTestData(480, 1);
    auto result = buffer->PushReferenceSignal(data.data(), -1);
    EXPECT_TRUE(result.isError());
}

// ============================================================================
// Get Delayed Reference Tests
// ============================================================================

TEST_F(ReferenceSignalBufferTest, GetDelayedReference) {
    auto config = CreateTestConfig();
    // 48kHz, 30ms delay = 1440 samples delay
    // Need to buffer at least 1440 + 480 = 1920 samples
    auto buf_result = ReferenceSignalBuffer::Create(config);
    ASSERT_TRUE(buf_result.isOk());
    auto buffer = std::move(buf_result.value());

    // Push enough data to get delayed reference
    for (int i = 0; i < 5; ++i) {
        auto data = CreateTestData(480, 1, 100);
        buffer->PushReferenceSignal(data.data(), 480);
    }

    // Should have 2400 samples (5 frames * 480)
    // 30ms delay @ 48kHz = 1440 samples
    // We can retrieve from position 1440 onwards
    auto ref_result = buffer->GetDelayedReference(480);
    EXPECT_TRUE(ref_result.isOk()) << "Should retrieve delayed reference";
    
    if (ref_result.isOk()) {
        auto ref = std::move(ref_result.value());
        EXPECT_EQ(ref.num_samples, 480);
        EXPECT_EQ(ref.data.size(), 480);  // 1 channel
    }
}

TEST_F(ReferenceSignalBufferTest, GetDelayedReferenceUnderflow) {
    auto config = CreateTestConfig();
    auto buf_result = ReferenceSignalBuffer::Create(config);
    ASSERT_TRUE(buf_result.isOk());
    auto buffer = std::move(buf_result.value());

    // Push only 1 frame (not enough for delay + data)
    auto data = CreateTestData(480, 1);
    buffer->PushReferenceSignal(data.data(), 480);

    // Try to get delayed reference (need 1440 + 480 = 1920 samples)
    auto ref_result = buffer->GetDelayedReference(480);
    EXPECT_TRUE(ref_result.isError()) << "Should underflow";
    EXPECT_EQ(ref_result.error(), AudioChatError::BufferUnderflow);
}

TEST_F(ReferenceSignalBufferTest, GetDelayedReferenceZeroDelay) {
    auto config = CreateTestConfig();
    config.delay_ms = 0;  // No delay
    auto buf_result = ReferenceSignalBuffer::Create(config);
    ASSERT_TRUE(buf_result.isOk());
    auto buffer = std::move(buf_result.value());

    // With zero delay, we just need 480 samples
    auto data = CreateTestData(480, 1, 42);
    buffer->PushReferenceSignal(data.data(), 480);

    auto ref_result = buffer->GetDelayedReference(480);
    EXPECT_TRUE(ref_result.isOk());
    
    if (ref_result.isOk()) {
        auto ref = std::move(ref_result.value());
        EXPECT_EQ(ref.data[0], 42);  // Should match input
    }
}

// ============================================================================
// Buffer Management Tests
// ============================================================================

TEST_F(ReferenceSignalBufferTest, GetBufferedSamples) {
    auto config = CreateTestConfig();
    auto buf_result = ReferenceSignalBuffer::Create(config);
    ASSERT_TRUE(buf_result.isOk());
    auto buffer = std::move(buf_result.value());

    EXPECT_EQ(buffer->GetBufferedSamples(), 0);

    auto data = CreateTestData(480, 1);
    buffer->PushReferenceSignal(data.data(), 480);
    EXPECT_EQ(buffer->GetBufferedSamples(), 480);

    buffer->PushReferenceSignal(data.data(), 480);
    EXPECT_EQ(buffer->GetBufferedSamples(), 960);
}

TEST_F(ReferenceSignalBufferTest, HasSufficientData) {
    auto config = CreateTestConfig();
    auto buf_result = ReferenceSignalBuffer::Create(config);
    ASSERT_TRUE(buf_result.isOk());
    auto buffer = std::move(buf_result.value());

    EXPECT_FALSE(buffer->HasSufficientData(480));

    // Push 5 frames = 2400 samples
    // 30ms delay = 1440 samples
    // Can retrieve 480 from position 1440 onwards = 1920 samples needed
    for (int i = 0; i < 5; ++i) {
        auto data = CreateTestData(480, 1);
        buffer->PushReferenceSignal(data.data(), 480);
    }

    EXPECT_TRUE(buffer->HasSufficientData(480));
}

TEST_F(ReferenceSignalBufferTest, BufferOverflow) {
    auto config = CreateTestConfig();
    config.buffer_duration_ms = 100;  // Small buffer for testing
    auto buf_result = ReferenceSignalBuffer::Create(config);
    ASSERT_TRUE(buf_result.isOk());
    auto buffer = std::move(buf_result.value());

    // Push enough to overflow
    // 100ms @ 48kHz = 4800 samples
    for (int i = 0; i < 20; ++i) {
        auto data = CreateTestData(480, 1);
        // Should succeed but oldest data discarded
        auto result = buffer->PushReferenceSignal(data.data(), 480);
        EXPECT_TRUE(result.isOk());
    }

    // Buffer should be at max (not growing indefinitely)
    EXPECT_LE(buffer->GetBufferedSamples(), 4800 + 480);  // Max + one frame margin
}

TEST_F(ReferenceSignalBufferTest, Clear) {
    auto config = CreateTestConfig();
    auto buf_result = ReferenceSignalBuffer::Create(config);
    ASSERT_TRUE(buf_result.isOk());
    auto buffer = std::move(buf_result.value());

    auto data = CreateTestData(480, 1);
    buffer->PushReferenceSignal(data.data(), 480);
    EXPECT_GT(buffer->GetBufferedSamples(), 0);

    buffer->Clear();
    EXPECT_EQ(buffer->GetBufferedSamples(), 0);
}

// ============================================================================
// Statistics and Utility Tests
// ============================================================================

TEST_F(ReferenceSignalBufferTest, GetStatistics) {
    auto config = CreateTestConfig();
    auto buf_result = ReferenceSignalBuffer::Create(config);
    ASSERT_TRUE(buf_result.isOk());
    auto buffer = std::move(buf_result.value());

    std::string stats = buffer->GetStatistics();
    EXPECT_FALSE(stats.empty());
    EXPECT_TRUE(stats.find("ReferenceSignalBuffer Statistics") != std::string::npos);
    EXPECT_TRUE(stats.find("48000") != std::string::npos);  // Sample rate
    EXPECT_TRUE(stats.find("30") != std::string::npos);     // Delay
}

TEST_F(ReferenceSignalBufferTest, GetConfig) {
    auto config = CreateTestConfig();
    config.delay_ms = 50;
    
    auto buf_result = ReferenceSignalBuffer::Create(config);
    ASSERT_TRUE(buf_result.isOk());
    auto buffer = std::move(buf_result.value());

    const auto& returned_config = buffer->GetConfig();
    EXPECT_EQ(returned_config.delay_ms, 50);
    EXPECT_EQ(returned_config.sample_rate_hz, 48000);
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(ReferenceSignalBufferTest, SequentialDelayedRetrieval) {
    auto config = CreateTestConfig();
    auto buf_result = ReferenceSignalBuffer::Create(config);
    ASSERT_TRUE(buf_result.isOk());
    auto buffer = std::move(buf_result.value());

    // Simulate streaming: push frames and retrieve delayed versions
    for (int frame_idx = 0; frame_idx < 10; ++frame_idx) {
        // Push new frame
        auto data = CreateTestData(480, 1, frame_idx);
        auto push_result = buffer->PushReferenceSignal(data.data(), 480);
        EXPECT_TRUE(push_result.isOk());

        // After enough frames, retrieve delayed reference
        if (frame_idx >= 3) {  // Need at least delay + frame
            auto ref_result = buffer->GetDelayedReference(480);
            EXPECT_TRUE(ref_result.isOk());
        }
    }
}

TEST_F(ReferenceSignalBufferTest, VariableSampleRates) {
    for (int sr : {8000, 16000, 32000, 48000}) {
        auto config = CreateTestConfig();
        config.sample_rate_hz = sr;
        int samples_per_frame = sr / 100;  // 10ms frame
        
        auto buf_result = ReferenceSignalBuffer::Create(config);
        ASSERT_TRUE(buf_result.isOk());
        auto buffer = std::move(buf_result.value());
        
        // Push 5 frames
        for (int i = 0; i < 5; ++i) {
            auto data = CreateTestData(samples_per_frame, 1);
            auto result = buffer->PushReferenceSignal(data.data(), samples_per_frame);
            EXPECT_TRUE(result.isOk()) << "Failed for sample rate " << sr;
        }
    }
}
