#include "AudioHandler.h"

#include "AudioRecorder.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace {

bool isAutoDevice(const std::string& name) {
    return name.empty() || name == "auto" || name == "default";
}

} // namespace

AudioHandler::AudioHandler(int input_sample_rate, int output_sample_rate,
                           int input_frames_per_buffer, int output_frames_per_buffer,
                           const char* input_name, const char* output_name,
                           HANDLE capture_ready_event, bool auto_start)
    : capture_queue_(64, static_cast<size_t>(std::max(1, input_frames_per_buffer))),
      playback_queue_(64, static_cast<size_t>(std::max(1, output_frames_per_buffer))),
      capture_ready_event_(capture_ready_event),
      input_device_name_(input_name ? input_name : ""),
      output_device_name_(output_name ? output_name : ""),
      input_sample_rate_(input_sample_rate),
      output_sample_rate_(output_sample_rate),
      input_frames_per_buffer_(input_frames_per_buffer),
      output_frames_per_buffer_(output_frames_per_buffer) {
    if (input_sample_rate_ <= 0 || output_sample_rate_ <= 0 ||
        input_frames_per_buffer_ <= 0 || output_frames_per_buffer_ <= 0) {
        throw std::invalid_argument("Invalid audio stream configuration");
    }

    const PaError init_error = Pa_Initialize();
    if (init_error != paNoError) {
        throw std::runtime_error(std::string("Pa_Initialize failed: ") +
                                 Pa_GetErrorText(init_error));
    }
    portaudio_initialized_ = true;

    try {
        selectValidDevices();

        PaStreamParameters input_parameters{};
        input_parameters.device = input_device_;
        input_parameters.channelCount = 1;
        input_parameters.sampleFormat = paInt16;
        input_parameters.suggestedLatency =
            Pa_GetDeviceInfo(input_parameters.device)->defaultLowInputLatency;

        PaStreamParameters output_parameters{};
        output_parameters.device = output_device_;
        output_parameters.channelCount = 1;
        output_parameters.sampleFormat = paInt16;
        output_parameters.suggestedLatency =
            Pa_GetDeviceInfo(output_parameters.device)->defaultLowOutputLatency;

        PaError error = Pa_OpenStream(&input_stream_, &input_parameters, nullptr,
                                      input_sample_rate_, input_frames_per_buffer_,
                                      paClipOff, audioCallback, this);
        if (error != paNoError) {
            throw std::runtime_error(std::string("Pa_OpenStream (input) failed: ") +
                                     Pa_GetErrorText(error));
        }

        error = Pa_OpenStream(&output_stream_, nullptr, &output_parameters,
                              output_sample_rate_, output_frames_per_buffer_,
                              paClipOff, nullptr, nullptr);
        if (error != paNoError) {
            throw std::runtime_error(std::string("Pa_OpenStream (output) failed: ") +
                                     Pa_GetErrorText(error));
        }

        if (auto_start) {
            start();
        }
    } catch (...) {
        cleanupStreams();
        if (portaudio_initialized_) {
            Pa_Terminate();
            portaudio_initialized_ = false;
        }
        throw;
    }
}

AudioHandler::AudioHandler(int frame_cache_time, const char* input_name,
                           const char* output_name, HANDLE capture_ready_event,
                           bool auto_start)
    : AudioHandler(48000, 48000, 48000 * frame_cache_time / 1000,
                   48000 * frame_cache_time / 1000, input_name, output_name,
                   capture_ready_event, auto_start) {
}

AudioHandler::~AudioHandler() {
    stop();
    if (portaudio_initialized_) {
        Pa_Terminate();
        portaudio_initialized_ = false;
    }
}

int AudioHandler::audioCallback(const void* input, void* output, unsigned long frame_count,
                                const PaStreamCallbackTimeInfo* time_info,
                                PaStreamCallbackFlags status_flags, void* user_data) {
    (void)output;
    (void)time_info;
    (void)status_flags;

    auto* handler = static_cast<AudioHandler*>(user_data);
    if (!handler || handler->stopped_.load() || !input) {
        return paContinue;
    }

    int16_t* slot = handler->capture_queue_.tryAcquireWrite();
    if (slot) {
        const size_t capacity = handler->capture_queue_.elemsPerSlot();
        const size_t count = std::min(capacity, static_cast<size_t>(frame_count));
        std::memset(slot, 0, capacity * sizeof(int16_t));
        std::memcpy(slot, input, count * sizeof(int16_t));
        handler->capture_queue_.commitWrite();
    }

    if (handler->capture_ready_event_) {
        SetEvent(handler->capture_ready_event_);
    }
    return paContinue;
}

bool AudioHandler::readCaptureFrame(int16_t* destination, size_t capacity_samples) {
    if (!destination || capacity_samples < static_cast<size_t>(input_frames_per_buffer_)) {
        return false;
    }
    return capture_queue_.tryReadTo(destination);
}

bool AudioHandler::readLatestCaptureFrame(int16_t* destination, size_t capacity_samples) {
    if (!destination || capacity_samples < static_cast<size_t>(input_frames_per_buffer_)) {
        return false;
    }

    bool has_frame = false;
    while (capture_queue_.tryReadTo(destination)) {
        has_frame = true;
    }
    return has_frame;
}

void AudioHandler::discardCaptureFrames() {
    while (capture_queue_.tryAcquireRead()) {
        capture_queue_.commitRead();
    }
}

void AudioHandler::start() {
    if (stopped_.load()) {
        throw std::runtime_error("AudioHandler is stopped");
    }
    if (!input_stream_ || !output_stream_) {
        throw std::runtime_error("Audio streams are not open");
    }
    if (Pa_IsStreamActive(input_stream_) == 1 && Pa_IsStreamActive(output_stream_) == 1) {
        return;
    }

    PaError error = Pa_StartStream(input_stream_);
    if (error != paNoError) {
        throw std::runtime_error(std::string("Pa_StartStream (input) failed: ") +
                                 Pa_GetErrorText(error));
    }

    error = Pa_StartStream(output_stream_);
    if (error != paNoError) {
        Pa_StopStream(input_stream_);
        throw std::runtime_error(std::string("Pa_StartStream (output) failed: ") +
                                 Pa_GetErrorText(error));
    }
}

void AudioHandler::play(const int16_t* data, int32_t sample_count) {
    if (stopped_.load() || !data || sample_count <= 0) {
        return;
    }

    if (recording_.load() && sample_count == output_frames_per_buffer_) {
        playback_queue_.tryWriteFrom(data);
    }

    const PaError error = Pa_WriteStream(output_stream_, data, sample_count);
    if (error != paNoError) {
        std::cerr << "Pa_WriteStream failed: " << Pa_GetErrorText(error) << std::endl;
    }
}

void AudioHandler::startRecordeAudio() {
    if (stopped_.load() || recording_.exchange(true)) {
        return;
    }
    try {
        record_thread_ = std::thread(&AudioHandler::recordingLoop, this);
    } catch (...) {
        recording_ = false;
        throw;
    }
}

void AudioHandler::stopRecordeAudio() {
    recording_ = false;
    if (record_thread_.joinable()) {
        if (std::this_thread::get_id() == record_thread_.get_id()) {
            return;
        }
        record_thread_.join();
    }
}

void AudioHandler::recordingLoop() {
    try {
        AudioRecorder recorder(output_sample_rate_, 1);
        std::vector<int16_t> capture(static_cast<size_t>(output_frames_per_buffer_));
        std::vector<int16_t> playback(static_cast<size_t>(output_frames_per_buffer_));
        recorder.start();

        while (recording_.load() && !stopped_.load()) {
            int16_t* capture_slot = capture_queue_.tryAcquireRead();
            int16_t* playback_slot = playback_queue_.tryAcquireRead();
            if (capture_slot) {
                std::memcpy(capture.data(), capture_slot, capture.size() * sizeof(int16_t));
                capture_queue_.commitRead();
            }
            if (playback_slot) {
                std::memcpy(playback.data(), playback_slot, playback.size() * sizeof(int16_t));
                playback_queue_.commitRead();
            }
            if (capture_slot && playback_slot) {
                recorder.recordMixed(capture, playback);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "Audio recording failed: " << error.what() << std::endl;
        recording_ = false;
    }
}

void AudioHandler::stop() {
    stopRecordeAudio();
    if (stopped_.exchange(true)) {
        return;
    }
    cleanupStreams();
}

void AudioHandler::cleanupStreams() {
    if (input_stream_) {
        if (Pa_IsStreamActive(input_stream_) == 1) {
            Pa_StopStream(input_stream_);
        }
        Pa_CloseStream(input_stream_);
        input_stream_ = nullptr;
    }
    if (output_stream_) {
        if (Pa_IsStreamActive(output_stream_) == 1) {
            Pa_StopStream(output_stream_);
        }
        Pa_CloseStream(output_stream_);
        output_stream_ = nullptr;
    }
}

void AudioHandler::selectValidDevices() {
    const int count = Pa_GetDeviceCount();
    if (count < 0) {
        throw std::runtime_error(std::string("Pa_GetDeviceCount failed: ") +
                                 Pa_GetErrorText(count));
    }

    input_device_ = isAutoDevice(input_device_name_) ? Pa_GetDefaultInputDevice() : paNoDevice;
    output_device_ = isAutoDevice(output_device_name_) ? Pa_GetDefaultOutputDevice() : paNoDevice;

    if (!isAutoDevice(input_device_name_) || !isAutoDevice(output_device_name_)) {
        for (int index = 0; index < count; ++index) {
            const PaDeviceInfo* info = Pa_GetDeviceInfo(index);
            if (!info) {
                continue;
            }
            if (!isAutoDevice(input_device_name_) && info->maxInputChannels > 0 &&
                input_device_name_ == info->name) {
                input_device_ = index;
            }
            if (!isAutoDevice(output_device_name_) && info->maxOutputChannels > 0 &&
                output_device_name_ == info->name) {
                output_device_ = index;
            }
        }
    }

    if (input_device_ == paNoDevice || !Pa_GetDeviceInfo(input_device_) ||
        Pa_GetDeviceInfo(input_device_)->maxInputChannels < 1) {
        throw std::runtime_error("No matching input audio device");
    }
    if (output_device_ == paNoDevice || !Pa_GetDeviceInfo(output_device_) ||
        Pa_GetDeviceInfo(output_device_)->maxOutputChannels < 1) {
        throw std::runtime_error("No matching output audio device");
    }
}
