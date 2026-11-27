#include <gtest/gtest.h>
#include "Codec.h"
#include "Logger.h"
#include <cstring>
#include <vector>

class CodecTest : public ::testing::Test {
protected:
    static const int SAMPLE_RATE = 48000;
    static const int CHANNELS = 1;
    
    void SetUp() override {
        Logger::init("test_logs", "error");  // Suppress logs during tests
    }

    void TearDown() override {
        Logger::shutdown();
    }
};

TEST_F(CodecTest, CodecInitialization) {
    EXPECT_NO_THROW({
        Codec codec(SAMPLE_RATE, CHANNELS);
    });
}

TEST_F(CodecTest, CodecEncodeDecodeRoundTrip) {
    Codec codec(SAMPLE_RATE, CHANNELS);
    
    // Create test PCM data (20ms frame at 48kHz = 960 samples)
    int samples_per_frame = SAMPLE_RATE / 50;  // 20ms
    std::vector<int16_t> original_pcm(samples_per_frame, 0);
    
    // Fill with test pattern
    for (int i = 0; i < samples_per_frame; ++i) {
        original_pcm[i] = static_cast<int16_t>((i % 1000) - 500);
    }
    
    // Encode
    std::vector<uint8_t> encoded(samples_per_frame * 2);  // Max size
    int32_t encoded_size = 0;
    
    EXPECT_NO_THROW({
        codec.encode(original_pcm.data(), encoded.data(), 
                    encoded.size(), encoded_size);
    });
    
    EXPECT_GT(encoded_size, 0);
    EXPECT_LE(encoded_size, static_cast<int32_t>(encoded.size()));
    
    // Decode
    std::vector<int16_t> decoded_pcm(samples_per_frame, 0);
    int32_t decoded_size = 0;
    
    EXPECT_NO_THROW({
        codec.decode(encoded.data(), encoded_size, decoded_pcm.data(),
                    decoded_pcm.size(), decoded_size);
    });
    
    EXPECT_GT(decoded_size, 0);
}

TEST_F(CodecTest, CodecGetters) {
    Codec codec(SAMPLE_RATE, CHANNELS);
    
    EXPECT_NE(codec.getEncoder(), nullptr);
    EXPECT_NE(codec.getDecoder(), nullptr);
    EXPECT_EQ(codec.getFramesPerBuffer(), SAMPLE_RATE / 50);
}

TEST_F(CodecTest, MultipleFrames) {
    Codec codec(SAMPLE_RATE, CHANNELS);
    int samples_per_frame = SAMPLE_RATE / 50;
    
    std::vector<int16_t> pcm(samples_per_frame);
    std::vector<uint8_t> encoded(samples_per_frame * 2);
    
    // Encode multiple frames
    for (int i = 0; i < 5; ++i) {
        // Fill with different pattern each time
        for (int j = 0; j < samples_per_frame; ++j) {
            pcm[j] = static_cast<int16_t>((i * 1000 + j) % 10000 - 5000);
        }
        
        int32_t encoded_size = 0;
        EXPECT_NO_THROW({
            codec.encode(pcm.data(), encoded.data(), 
                        encoded.size(), encoded_size);
        });
        EXPECT_GT(encoded_size, 0);
    }
}

TEST_F(CodecTest, TenMillisecondFrames) {
    Codec codec(SAMPLE_RATE, CHANNELS, 10);
    const int samples = SAMPLE_RATE / 100;
    std::vector<int16_t> pcm(samples, 100);
    std::vector<uint8_t> encoded(samples * 2);
    std::vector<int16_t> decoded(samples);
    int32_t encoded_size = 0;
    int32_t decoded_size = 0;

    ASSERT_NO_THROW(codec.encode(pcm.data(), encoded.data(), encoded.size(), encoded_size));
    ASSERT_GT(encoded_size, 0);
    ASSERT_NO_THROW(codec.decode(encoded.data(), encoded_size, decoded.data(),
                                 decoded.size(), decoded_size));
    EXPECT_EQ(decoded_size, samples);
    EXPECT_EQ(codec.getFramesPerBuffer(), samples);
}
