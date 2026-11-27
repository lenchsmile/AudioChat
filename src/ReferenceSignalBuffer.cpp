#include "ReferenceSignalBuffer.h"
#include "Logger.h"

#include <algorithm>
#include <limits>
#include <sstream>

namespace AudioChat {

Result<void> ReferenceSignalBuffer::Config::Validate() const {
    if (sample_rate_hz != 8000 && sample_rate_hz != 16000 &&
        sample_rate_hz != 32000 && sample_rate_hz != 48000) {
        return Result<void>::error(AudioChatError::InvalidCodecConfig,
                                   "Invalid sample rate: " + std::to_string(sample_rate_hz));
    }
    if (num_channels < 1 || num_channels > 2) {
        return Result<void>::error(AudioChatError::InvalidCodecConfig,
                                   "Invalid channel count: " + std::to_string(num_channels));
    }
    if (frame_duration_ms != 10 && frame_duration_ms != 20) {
        return Result<void>::error(AudioChatError::InvalidCodecConfig,
                                   "Invalid frame duration: " + std::to_string(frame_duration_ms));
    }
    if (delay_ms < 0 || delay_ms > 200) {
        return Result<void>::error(AudioChatError::InvalidArgument,
                                   "Invalid delay: " + std::to_string(delay_ms));
    }
    if (buffer_duration_ms < 100 || buffer_duration_ms > 500) {
        return Result<void>::error(AudioChatError::InvalidArgument,
                                   "Invalid buffer duration: " + std::to_string(buffer_duration_ms));
    }
    if (buffer_duration_ms <= delay_ms) {
        return Result<void>::error(
            AudioChatError::InvalidArgument,
            "Buffer duration must be greater than delay");
    }
    return Result<void>::ok();
}

Result<std::unique_ptr<ReferenceSignalBuffer>> ReferenceSignalBuffer::Create(const Config& config) {
    const auto validation = config.Validate();
    if (validation.isError()) {
        return Result<std::unique_ptr<ReferenceSignalBuffer>>::error(
            validation.error(), validation.message());
    }

    auto buffer = std::unique_ptr<ReferenceSignalBuffer>(new ReferenceSignalBuffer(config));
    const auto initialized = buffer->Initialize();
    if (initialized.isError()) {
        return Result<std::unique_ptr<ReferenceSignalBuffer>>::error(
            initialized.error(), initialized.message());
    }
    return Result<std::unique_ptr<ReferenceSignalBuffer>>::ok(std::move(buffer));
}

ReferenceSignalBuffer::ReferenceSignalBuffer(const Config& config) : config_(config) {
    LOG_INFO("ReferenceSignalBuffer created: {}Hz, {} channels, {}ms delay, {}ms buffer",
             config_.sample_rate_hz, config_.num_channels, config_.delay_ms,
             config_.buffer_duration_ms);
}

Result<void> ReferenceSignalBuffer::Initialize() {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    buffer_.clear();
    buffer_start_sample_ = 0;
    total_pushed_samples_ = 0;
    next_read_sample_ = 0;
    last_push_timestamp_ms_ = 0;
    is_initialized_ = true;
    LOG_INFO("ReferenceSignalBuffer initialized: {} interleaved samples capacity",
             GetMaxBufferSamples());
    return Result<void>::ok();
}

int32_t ReferenceSignalBuffer::GetDelaySamples() const {
    return (config_.sample_rate_hz * config_.delay_ms) / 1000;
}

int32_t ReferenceSignalBuffer::GetMaxBufferSamples() const {
    return (config_.sample_rate_hz * config_.buffer_duration_ms / 1000) * config_.num_channels;
}

Result<void> ReferenceSignalBuffer::PushReferenceSignal(const int16_t* data, int32_t num_samples) {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    if (!is_initialized_) {
        return Result<void>::error(AudioChatError::NotInitialized,
                                   "Reference signal buffer not initialized");
    }
    if (!data || num_samples <= 0) {
        return Result<void>::error(AudioChatError::InvalidArgument,
                                   "Invalid reference signal");
    }
    if (num_samples > std::numeric_limits<int32_t>::max() / config_.num_channels) {
        return Result<void>::error(AudioChatError::BufferOverflow,
                                   "Reference signal is too large");
    }

    const size_t interleaved_samples = static_cast<size_t>(num_samples) * config_.num_channels;
    buffer_.insert(buffer_.end(), data, data + interleaved_samples);
    total_pushed_samples_ += static_cast<uint64_t>(num_samples);
    last_push_timestamp_ms_ = total_pushed_samples_ * 1000ULL / config_.sample_rate_hz;
    ++frames_pushed_;

    const size_t maximum = static_cast<size_t>(GetMaxBufferSamples());
    while (buffer_.size() > maximum) {
        for (int channel = 0; channel < config_.num_channels; ++channel) {
            buffer_.pop_front();
        }
        ++buffer_start_sample_;
    }
    next_read_sample_ = std::max(next_read_sample_, buffer_start_sample_);
    return Result<void>::ok();
}

Result<ReferenceSignalBuffer::ReferenceFrame> ReferenceSignalBuffer::GetDelayedReference(
    int32_t num_samples) {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    if (!is_initialized_) {
        return Result<ReferenceFrame>::error(AudioChatError::NotInitialized,
                                             "Reference signal buffer not initialized");
    }
    if (num_samples <= 0 ||
        num_samples > std::numeric_limits<int32_t>::max() / config_.num_channels) {
        return Result<ReferenceFrame>::error(AudioChatError::InvalidArgument,
                                             "Invalid reference frame size");
    }

    const uint64_t delay_samples = static_cast<uint64_t>(GetDelaySamples());
    const uint64_t requested = static_cast<uint64_t>(num_samples);
    if (total_pushed_samples_ < delay_samples + requested) {
        return Result<ReferenceFrame>::error(AudioChatError::BufferUnderflow,
                                             "Insufficient buffered reference signal");
    }

    const uint64_t start = total_pushed_samples_ - delay_samples - requested;
    if (start < next_read_sample_ || start < buffer_start_sample_) {
        return Result<ReferenceFrame>::error(AudioChatError::BufferUnderflow,
                                             "No new delayed reference signal available");
    }

    const size_t offset = static_cast<size_t>(start - buffer_start_sample_) * config_.num_channels;
    const size_t count = static_cast<size_t>(num_samples) * config_.num_channels;
    if (offset > buffer_.size() || count > buffer_.size() - offset) {
        return Result<ReferenceFrame>::error(AudioChatError::BufferUnderflow,
                                             "Reference signal was discarded");
    }

    ReferenceFrame frame;
    frame.num_samples = num_samples;
    frame.timestamp_ms = start * 1000ULL / config_.sample_rate_hz;
    frame.data.reserve(count);
    auto begin = buffer_.begin();
    std::advance(begin, static_cast<std::ptrdiff_t>(offset));
    frame.data.insert(frame.data.end(), begin, begin + static_cast<std::ptrdiff_t>(count));
    next_read_sample_ = start + requested;
    ++frames_retrieved_;
    return Result<ReferenceFrame>::ok(std::move(frame));
}

int32_t ReferenceSignalBuffer::GetBufferedSamples() const {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    return static_cast<int32_t>(buffer_.size() / config_.num_channels);
}

std::string ReferenceSignalBuffer::GetStatistics() const {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    std::ostringstream stream;
    stream << "ReferenceSignalBuffer Statistics:\n"
           << "  Sample rate: " << config_.sample_rate_hz << " Hz\n"
           << "  Channels: " << config_.num_channels << "\n"
           << "  Frame duration: " << config_.frame_duration_ms << " ms\n"
           << "  Configured delay: " << config_.delay_ms << " ms ("
           << GetDelaySamples() << " samples/channel)\n"
           << "  Buffer duration: " << config_.buffer_duration_ms << " ms\n"
           << "  Max buffer size: " << GetMaxBufferSamples() << " interleaved samples\n"
           << "  Current buffer fill: " << buffer_.size() << " interleaved samples\n"
           << "  Frames pushed: " << frames_pushed_ << "\n"
           << "  Frames retrieved: " << frames_retrieved_;
    return stream.str();
}

void ReferenceSignalBuffer::Clear() {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    buffer_.clear();
    buffer_start_sample_ = 0;
    total_pushed_samples_ = 0;
    next_read_sample_ = 0;
    last_push_timestamp_ms_ = 0;
    frames_pushed_ = 0;
    frames_retrieved_ = 0;
}

bool ReferenceSignalBuffer::HasSufficientData(int32_t num_samples) const {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    if (!is_initialized_ || num_samples <= 0 ||
        num_samples > std::numeric_limits<int32_t>::max() / config_.num_channels) {
        return false;
    }
    const uint64_t delay_samples = static_cast<uint64_t>(GetDelaySamples());
    const uint64_t requested = static_cast<uint64_t>(num_samples);
    if (total_pushed_samples_ < delay_samples + requested) {
        return false;
    }
    const uint64_t start = total_pushed_samples_ - delay_samples - requested;
    return start >= next_read_sample_ && start >= buffer_start_sample_;
}

} // namespace AudioChat
