#include "AecProcessor.h"
#include "AudioHandler.h"
#include "Logger.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

uint32_t nextRandom(uint32_t& state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

double energy(const std::vector<int16_t>& samples) {
    double sum = 0.0;
    for (const int16_t sample : samples) {
        const double value = sample;
        sum += value * value;
    }
    return sum;
}

} // namespace

int main(int argc, char** argv) {
    using namespace AudioChat;
    Logger::init("logs", "info");

    int duration_seconds = 5;
    double playback_gain = 0.25;
    std::string input_device = "auto";
    std::string output_device = "auto";

    try {
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            auto value = [&](const char* name) -> std::string {
                if (index + 1 >= argc) {
                    throw std::invalid_argument(std::string("Missing value for ") + name);
                }
                return argv[++index];
            };
            if (argument == "--duration") {
                duration_seconds = std::stoi(value("--duration"));
            } else if (argument == "--gain") {
                playback_gain = std::stod(value("--gain"));
            } else if (argument == "--input-device") {
                input_device = value("--input-device");
            } else if (argument == "--output-device") {
                output_device = value("--output-device");
            } else if (argument == "--help") {
                std::cout << "Usage: AudioChatAecCheck [--duration seconds] [--gain 0.25] "
                             "[--input-device name] [--output-device name]\n"
                             "Use speakers at a moderate volume; headphones cannot measure acoustic echo.\n";
                return 0;
            } else {
                throw std::invalid_argument("Unknown option: " + argument);
            }
        }

        if (duration_seconds < 2 || duration_seconds > 60 ||
            !std::isfinite(playback_gain) || playback_gain <= 0.0 || playback_gain > 1.0) {
            throw std::invalid_argument("Duration must be 2-60 seconds and gain must be in (0,1]");
        }

        AecConfig config;
        config.sample_rate_hz = 48000;
        config.num_channels = 1;
        config.frame_duration_ms = 10;
        config.enable_ns = false;
        config.enable_agc = false;

        auto processor = AecProcessor::Create(config);
        if (processor.isError() || !processor.value()->IsBackendActive()) {
            throw std::runtime_error("A real WebRTC AEC backend is required");
        }

        AudioHandler audio(48000, 48000, 480, 480,
                           input_device.c_str(), output_device.c_str());
        constexpr int frame_samples = 480;
        const int frame_count = duration_seconds * 100;
        const int measurement_start_frame = 100;

        uint32_t random_state = 0x5eed1234u;
        double input_energy = 0.0;
        double output_energy = 0.0;
        int measured_frames = 0;

        std::cout << "Playing controlled reference noise for " << duration_seconds
                  << " seconds. Keep speakers and microphone in their normal positions.\n";

        for (int frame = 0; frame < frame_count; ++frame) {
            AudioFrame reference;
            reference.data.resize(frame_samples);
            reference.sample_count = frame_samples;
            reference.channels = 1;
            for (auto& sample : reference.data) {
                const int32_t noise = static_cast<int32_t>(nextRandom(random_state) % 20001) - 10000;
                sample = static_cast<int16_t>(noise * playback_gain);
            }

            audio.play(reference.data.data(), frame_samples);

            std::vector<int16_t> capture(frame_samples);
            const auto capture_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            while (!audio.readCaptureFrame(capture.data(), capture.size())) {
                if (std::chrono::steady_clock::now() >= capture_deadline) {
                    throw std::runtime_error("Timed out waiting for a microphone frame");
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            // Pair the reference with the captured frame before queuing the
            // next one. A capture underrun must never strand a pending reference.
            auto reverse_result = processor.value()->ProcessReferenceStream(reference);
            if (reverse_result.isError()) {
                throw std::runtime_error(reverse_result.message());
            }

            AudioFrame capture_frame;
            capture_frame.data = std::move(capture);
            capture_frame.sample_count = frame_samples;
            capture_frame.channels = 1;
            auto processed = processor.value()->ProcessCaptureStream(capture_frame);
            if (processed.isError()) {
                throw std::runtime_error(processed.message());
            }

            if (frame >= measurement_start_frame) {
                input_energy += energy(capture_frame.data);
                output_energy += energy(processed.value().data);
                ++measured_frames;
            }
        }

        audio.stop();
        if (measured_frames == 0 || input_energy <= 0.0 || output_energy <= 0.0) {
            throw std::runtime_error("Insufficient captured audio for an ERLE measurement");
        }

        const double erle_db = 10.0 * std::log10(input_energy / output_energy);
        std::cout << "Measured frames: " << measured_frames << "\n"
                  << "Acoustic ERLE: " << erle_db << " dB\n"
                  << processor.value()->GetStatistics() << "\n";
        constexpr double minimum_erle_db = 10.0;
        if (erle_db <= minimum_erle_db) {
            throw std::runtime_error("Acoustic ERLE is below the 10 dB acceptance threshold");
        }
        return 0;
    } catch (const std::exception& error) {
        LOG_ERROR("Acoustic AEC check failed: {}", error.what());
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
