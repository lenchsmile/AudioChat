#pragma once

#include <portaudio.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "SpscFixedSlots.h"

class AudioHandler {
public:
    AudioHandler(int input_sample_rate, int output_sample_rate,
                 int input_frames_per_buffer, int output_frames_per_buffer,
                 const char* input_name, const char* output_name,
                 HANDLE capture_ready_event = nullptr);
    AudioHandler(int frame_cache_time, const char* input_name,
                 const char* output_name, HANDLE capture_ready_event = nullptr);

    AudioHandler(const AudioHandler&) = delete;
    AudioHandler& operator=(const AudioHandler&) = delete;
    AudioHandler(AudioHandler&&) = delete;
    AudioHandler& operator=(AudioHandler&&) = delete;

    ~AudioHandler();

    bool readCaptureFrame(int16_t* destination, size_t capacity_samples);
    void startRecordeAudio();
    void stopRecordeAudio();
    bool isRecording() const { return recording_.load(); }

    // data_size is a sample count per channel, not a byte count.
    void play(const int16_t* data, int32_t sample_count);
    void stop();

    int inputSampleRate() const { return input_sample_rate_; }
    int outputSampleRate() const { return output_sample_rate_; }
    int inputFramesPerBuffer() const { return input_frames_per_buffer_; }
    int outputFramesPerBuffer() const { return output_frames_per_buffer_; }

private:
    void selectValidDevices();
    void cleanupStreams();
    void recordingLoop();

    static int audioCallback(const void* input, void* output, unsigned long frame_count,
                             const PaStreamCallbackTimeInfo* time_info,
                             PaStreamCallbackFlags status_flags, void* user_data);

    std::atomic<bool> recording_{false};
    std::atomic<bool> stopped_{false};
    SpscFixedSlots<int16_t> capture_queue_;
    SpscFixedSlots<int16_t> playback_queue_;

    HANDLE capture_ready_event_ = nullptr;
    std::string input_device_name_;
    std::string output_device_name_;

    int input_sample_rate_ = 48000;
    int output_sample_rate_ = 48000;
    int input_frames_per_buffer_ = 0;
    int output_frames_per_buffer_ = 0;

    PaDeviceIndex input_device_ = paNoDevice;
    PaDeviceIndex output_device_ = paNoDevice;
    PaStream* input_stream_ = nullptr;
    PaStream* output_stream_ = nullptr;
    bool portaudio_initialized_ = false;
    std::thread record_thread_;
};
