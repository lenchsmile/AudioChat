#include <gtest/gtest.h>

#ifdef HAS_WEBRTC_AEC

#include "AecProcessor.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace AudioChat;

namespace {

uint32_t nextRandom(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

double energy(const std::vector<int16_t>& samples, size_t begin, size_t end) {
    double sum = 0.0;
    for (size_t index = begin; index < end; ++index) {
        const double sample = samples[index];
        sum += sample * sample;
    }
    return sum;
}

} // namespace

TEST(AecEffectTest, RealBackendIsActive) {
    AecConfig config;
    config.enable_ns = false;
    config.enable_agc = false;
    auto processor = AecProcessor::Create(config);
    ASSERT_TRUE(processor.isOk()) << processor.message();
    EXPECT_TRUE(processor.value()->IsBackendActive());
}

TEST(AecEffectTest, SuppressesDelayedSyntheticEcho) {
    AecConfig config;
    config.sample_rate_hz = 48000;
    config.num_channels = 1;
    config.frame_duration_ms = 10;
    config.stream_delay_ms = 0;
    config.enable_ns = false;
    config.enable_agc = false;

    auto processor = AecProcessor::Create(config);
    ASSERT_TRUE(processor.isOk()) << processor.message();
    ASSERT_TRUE(processor.value()->IsBackendActive());

    constexpr int frame_samples = 480;
    constexpr int frame_count = 200;
    constexpr int delay_samples = 64 * 48;
    constexpr double echo_gain = 0.5;

    uint32_t random_state = 0x12345678u;
    std::vector<int16_t> reference(static_cast<size_t>(frame_count * frame_samples));
    for (auto& sample : reference) {
        sample = static_cast<int16_t>(static_cast<int32_t>(nextRandom(random_state) % 20001) - 10000);
    }

    std::vector<int16_t> capture(reference.size(), 0);
    for (size_t index = delay_samples; index < capture.size(); ++index) {
        capture[index] = static_cast<int16_t>(reference[index - delay_samples] * echo_gain);
    }

    std::vector<int16_t> nearend(capture.size(), 0);
    const size_t double_talk_start = capture.size() * 3 / 4;
    for (size_t index = double_talk_start; index < capture.size(); ++index) {
        const double phase = 2.0 * 3.14159265358979323846 * 300.0 *
                             static_cast<double>(index) / config.sample_rate_hz;
        nearend[index] = static_cast<int16_t>(4000.0 * std::sin(phase));
        const int32_t mixed = static_cast<int32_t>(capture[index]) + nearend[index];
        capture[index] = static_cast<int16_t>(std::clamp(mixed, -32768, 32767));
    }

    std::vector<int16_t> output(reference.size(), 0);
    for (int frame = 0; frame < frame_count; ++frame) {
        const size_t offset = static_cast<size_t>(frame * frame_samples);
        AudioFrame reference_frame;
        reference_frame.data.assign(reference.begin() + static_cast<std::ptrdiff_t>(offset),
                                    reference.begin() + static_cast<std::ptrdiff_t>(offset + frame_samples));
        reference_frame.sample_count = frame_samples;
        reference_frame.channels = 1;

        AudioFrame capture_frame;
        capture_frame.data.assign(capture.begin() + static_cast<std::ptrdiff_t>(offset),
                                  capture.begin() + static_cast<std::ptrdiff_t>(offset + frame_samples));
        capture_frame.sample_count = frame_samples;
        capture_frame.channels = 1;

        ASSERT_TRUE(processor.value()->ProcessReferenceStream(reference_frame).isOk());
        auto processed = processor.value()->ProcessCaptureStream(capture_frame);
        ASSERT_TRUE(processed.isOk()) << processed.message();
        std::copy(processed.value().data.begin(), processed.value().data.end(),
                  output.begin() + static_cast<std::ptrdiff_t>(offset));
    }

    const size_t measurement_start = reference.size() / 2;
    const double input_energy = energy(capture, measurement_start, double_talk_start);
    const double output_energy = energy(output, measurement_start, double_talk_start);
    ASSERT_GT(input_energy, 0.0);
    ASSERT_GT(output_energy, 0.0);
    const double erle_db = 10.0 * std::log10(input_energy / output_energy);
    EXPECT_GT(erle_db, 8.0) << "Synthetic ERLE: " << erle_db << " dB";

    if (const auto webrtc_erle = processor.value()->GetEchoReturnLossEnhancement()) {
        EXPECT_GT(*webrtc_erle, 0.0) << "WebRTC reported ERLE: " << *webrtc_erle << " dB";
    }

    double input_projection = 0.0;
    double output_projection = 0.0;
    double nearend_energy = 0.0;
    const size_t double_talk_measurement_start = double_talk_start + 4800;
    for (size_t index = double_talk_measurement_start; index < nearend.size(); ++index) {
        input_projection += static_cast<double>(nearend[index]) * capture[index];
        output_projection += static_cast<double>(nearend[index]) * output[index];
        nearend_energy += static_cast<double>(nearend[index]) * nearend[index];
    }
    ASSERT_GT(input_projection, 0.0);
    double best_double_talk_correlation = -2.0;
    int best_double_talk_lag = 0;
    constexpr int correlation_margin = 960;
    for (int lag = -correlation_margin; lag <= correlation_margin; lag += 8) {
        double correlation = 0.0;
        for (size_t index = double_talk_measurement_start + correlation_margin;
             index + lag < nearend.size() - correlation_margin; ++index) {
            correlation += static_cast<double>(nearend[index]) * output[index + lag];
        }
        correlation /= nearend_energy;
        if (correlation > best_double_talk_correlation) {
            best_double_talk_correlation = correlation;
            best_double_talk_lag = lag;
        }
    }
    EXPECT_GT(best_double_talk_correlation, 0.6)
        << "Near-end speech was excessively suppressed during double-talk; "
        << "best_lag=" << best_double_talk_lag
        << ", best_correlation=" << best_double_talk_correlation
        << ", zero_lag_ratio=" << output_projection / input_projection;
}

TEST(AecEffectTest, PreservesNearEndWithoutEcho) {
    AecConfig config;
    config.enable_ns = false;
    config.enable_agc = false;
    auto processor = AecProcessor::Create(config);
    ASSERT_TRUE(processor.isOk()) << processor.message();
    ASSERT_TRUE(processor.value()->IsBackendActive());

    std::vector<int16_t> input_signal;
    std::vector<int16_t> output_signal;
    input_signal.reserve(48000);
    output_signal.reserve(48000);
    for (int frame = 0; frame < 100; ++frame) {
        AudioFrame reference;
        reference.data.assign(480, 0);
        reference.sample_count = 480;
        reference.channels = 1;

        AudioFrame capture;
        capture.data.resize(480);
        capture.sample_count = 480;
        capture.channels = 1;
        for (int index = 0; index < 480; ++index) {
            const double phase = 2.0 * 3.14159265358979323846 * 300.0 *
                                 static_cast<double>(frame * 480 + index) / config.sample_rate_hz;
            capture.data[index] = static_cast<int16_t>(4000.0 * std::sin(phase));
        }

        ASSERT_TRUE(processor.value()->ProcessReferenceStream(reference).isOk());
        auto processed = processor.value()->ProcessCaptureStream(capture);
        ASSERT_TRUE(processed.isOk()) << processed.message();
        input_signal.insert(input_signal.end(), capture.data.begin(), capture.data.end());
        output_signal.insert(output_signal.end(), processed.value().data.begin(), processed.value().data.end());
    }

    double input_projection = 0.0;
    for (const auto sample : input_signal) {
        input_projection += static_cast<double>(sample) * sample;
    }
    ASSERT_GT(input_projection, 0.0);
    double best_correlation = -2.0;
    int best_lag = 0;
    constexpr int margin = 960;
    for (int lag = -margin; lag <= margin; lag += 8) {
        double correlation = 0.0;
        for (size_t index = margin; index + lag < input_signal.size() - margin; ++index) {
            correlation += static_cast<double>(input_signal[index]) * output_signal[index + lag];
        }
        correlation /= input_projection;
        if (correlation > best_correlation) {
            best_correlation = correlation;
            best_lag = lag;
        }
    }
    EXPECT_GT(best_correlation, 0.8)
        << "Near-end signal damaged without echo; best_lag=" << best_lag
        << ", best_correlation=" << best_correlation;
    EXPECT_LE(std::abs(best_lag), 960)
        << "Near-end processing delay exceeds 20ms; best_lag=" << best_lag;
}

TEST(AecEffectTest, TwentyMillisecondProcessingSuppressesEcho) {
    AecConfig config;
    config.frame_duration_ms = 20;
    config.enable_ns = false;
    config.enable_agc = false;
    auto processor = AecProcessor::Create(config);
    ASSERT_TRUE(processor.isOk()) << processor.message();
    ASSERT_TRUE(processor.value()->IsBackendActive());

    constexpr int frame_samples = 960;
    constexpr int frame_count = 50;
    constexpr int delay_samples = 32 * 48;
    constexpr double echo_gain = 0.5;

    uint32_t random_state = 0x87654321u;
    std::vector<int16_t> reference(static_cast<size_t>(frame_count * frame_samples));
    for (auto& sample : reference) {
        sample = static_cast<int16_t>(static_cast<int32_t>(nextRandom(random_state) % 20001) - 10000);
    }

    std::vector<int16_t> capture(reference.size(), 0);
    for (size_t index = delay_samples; index < capture.size(); ++index) {
        capture[index] = static_cast<int16_t>(reference[index - delay_samples] * echo_gain);
    }

    std::vector<int16_t> output(reference.size(), 0);
    for (int frame = 0; frame < frame_count; ++frame) {
        const size_t offset = static_cast<size_t>(frame * frame_samples);
        AudioFrame reference_frame;
        reference_frame.data.assign(reference.begin() + static_cast<std::ptrdiff_t>(offset),
                                    reference.begin() + static_cast<std::ptrdiff_t>(offset + frame_samples));
        reference_frame.sample_count = frame_samples;
        reference_frame.channels = 1;

        AudioFrame capture_frame;
        capture_frame.data.assign(capture.begin() + static_cast<std::ptrdiff_t>(offset),
                                  capture.begin() + static_cast<std::ptrdiff_t>(offset + frame_samples));
        capture_frame.sample_count = frame_samples;
        capture_frame.channels = 1;

        ASSERT_TRUE(processor.value()->ProcessReferenceStream(reference_frame).isOk());
        auto processed = processor.value()->ProcessCaptureStream(capture_frame);
        ASSERT_TRUE(processed.isOk()) << processed.message();
        std::copy(processed.value().data.begin(), processed.value().data.end(),
                  output.begin() + static_cast<std::ptrdiff_t>(offset));
    }

    const size_t measurement_start = reference.size() / 2;
    const double input_energy = energy(capture, measurement_start, capture.size());
    const double output_energy = energy(output, measurement_start, output.size());
    ASSERT_GT(input_energy, 0.0);
    ASSERT_GT(output_energy, 0.0);
    const double erle_db = 10.0 * std::log10(input_energy / output_energy);
    EXPECT_GT(erle_db, 8.0) << "20ms synthetic ERLE: " << erle_db << " dB";
}

#endif // HAS_WEBRTC_AEC
