#include "ReferenceSignalBuffer.h"
#include "Logger.h"
#include <algorithm>
#include <sstream>

namespace AudioChat {

// ============================================================================
// Config Implementation
// ============================================================================

Result<void> ReferenceSignalBuffer::Config::Validate() const {
    // Validate sample rate
    if (sample_rate_hz != 8000 && sample_rate_hz != 16000 && 
        sample_rate_hz != 32000 && sample_rate_hz != 48000) {
        return Result<void>::error(
            AudioChatError::InvalidCodecConfig,
            "Invalid sample rate: " + std::to_string(sample_rate_hz)
        );
    }

    // Validate channels
    if (num_channels < 1 || num_channels > 2) {
        return Result<void>::error(
            AudioChatError::InvalidCodecConfig,
            "Invalid number of channels: " + std::to_string(num_channels)
        );
    }

    // Validate frame duration
    if (frame_duration_ms != 10 && frame_duration_ms != 20) {
        return Result<void>::error(
            AudioChatError::InvalidCodecConfig,
            "Invalid frame duration: " + std::to_string(frame_duration_ms) + " ms"
        );
    }

    // Validate delay
    if (delay_ms < 0 || delay_ms > 200) {
        return Result<void>::error(
            AudioChatError::InvalidArgument,
            "Invalid delay: " + std::to_string(delay_ms) + 
            " ms (must be 0-200ms)"
        );
    }

    // Validate buffer duration
    if (buffer_duration_ms < 100 || buffer_duration_ms > 500) {
        return Result<void>::error(
            AudioChatError::InvalidArgument,
            "Invalid buffer duration: " + std::to_string(buffer_duration_ms) + 
            " ms (must be 100-500ms)"
        );
    }

    // Buffer must be larger than delay
    if (buffer_duration_ms <= delay_ms) {
        return Result<void>::error(
            AudioChatError::InvalidArgument,
            "Buffer duration (" + std::to_string(buffer_duration_ms) + 
            "ms) must be greater than delay (" + std::to_string(delay_ms) + "ms)"
        );
    }

    return Result<void>::ok();
}

// ============================================================================
// ReferenceSignalBuffer Implementation
// ============================================================================

Result<std::unique_ptr<ReferenceSignalBuffer>> ReferenceSignalBuffer::Create(
    const Config& config) {
    
    // Validate config
    auto validation_result = config.Validate();
    if (validation_result.isError()) {
        LOG_ERROR("ReferenceSignalBuffer::Create: Config validation failed - {}", 
                  validation_result.message());
        return Result<std::unique_ptr<ReferenceSignalBuffer>>::error(
            validation_result.error(),
            validation_result.message()
        );
    }

    // Create buffer instance
    auto buffer = std::unique_ptr<ReferenceSignalBuffer>(
        new ReferenceSignalBuffer(config)
    );

    // Initialize
    auto init_result = buffer->Initialize();
    if (init_result.isError()) {
        LOG_ERROR("ReferenceSignalBuffer::Create: Initialization failed - {}", 
                  init_result.message());
        return Result<std::unique_ptr<ReferenceSignalBuffer>>::error(
            init_result.error(),
            init_result.message()
        );
    }

    return Result<std::unique_ptr<ReferenceSignalBuffer>>::ok(buffer.release());
}

ReferenceSignalBuffer::ReferenceSignalBuffer(const Config& config)
    : config_(config) {
    LOG_INFO("ReferenceSignalBuffer created: {}Hz, {} channels, {}ms delay, {}ms buffer",
             config.sample_rate_hz, config.num_channels, config.delay_ms, 
             config.buffer_duration_ms);
}

Result<void> ReferenceSignalBuffer::Initialize() {
    int32_t max_samples = GetMaxBufferSamples();
    
    try {
        // Pre-allocate buffer with maximum size
        buffer_.reserve(max_samples * config_.num_channels);
        is_initialized_ = true;
        LOG_INFO("ReferenceSignalBuffer initialized: {} samples total capacity",
                 max_samples * config_.num_channels);
        return Result<void>::ok();
    } catch (const std::exception& e) {
        LOG_ERROR("ReferenceSignalBuffer::Initialize failed: {}", e.what());
        return Result<void>::error(
            AudioChatError::BufferAllocFailed,
            std::string("Failed to allocate buffer: ") + e.what()
        );
    }
}

int32_t ReferenceSignalBuffer::GetDelaySamples() const {
    // delay_samples = sample_rate * delay_ms / 1000
    return (config_.sample_rate_hz * config_.delay_ms) / 1000;
}

int32_t ReferenceSignalBuffer::GetMaxBufferSamples() const {
    // max_buffer = sample_rate * buffer_duration_ms / 1000 * num_channels
    return (config_.sample_rate_hz * config_.buffer_duration_ms / 1000) * 
           config_.num_channels;
}

Result<void> ReferenceSignalBuffer::PushReferenceSignal(
    const int16_t* data, int32_t num_samples) {
    
    if (!is_initialized_) {
        return Result<void>::error(
            AudioChatError::NotInitialized,
            "Reference signal buffer not initialized"
        );
    }

    if (data == nullptr) {
        return Result<void>::error(
            AudioChatError::InvalidArgument,
            "Data pointer is null"
        );
    }

    if (num_samples <= 0) {
        return Result<void>::error(
            AudioChatError::InvalidArgument,
            "Invalid number of samples: " + std::to_string(num_samples)
        );
    }

    int32_t samples_to_add = num_samples * config_.num_channels;
    int32_t max_samples = GetMaxBufferSamples();

    // Check for buffer overflow
    if (static_cast<int32_t>(buffer_.size()) + samples_to_add > max_samples) {
        LOG_WARN("Reference signal buffer overflow: current={}, adding={}, max={}",
                 buffer_.size(), samples_to_add, max_samples);
        // Remove oldest data to make room (FIFO overflow behavior)
        int32_t remove_count = (buffer_.size() + samples_to_add) - max_samples;
        for (int32_t i = 0; i < remove_count && !buffer_.empty(); ++i) {
            buffer_.pop_front();
        }
    }

    // Add new samples to buffer
    buffer_.insert(buffer_.end(), data, data + samples_to_add);
    
    frames_pushed_++;
    last_push_timestamp_ms_ = 0;  // Would be set by caller if needed

    return Result<void>::ok();
}

Result<ReferenceSignalBuffer::ReferenceFrame> ReferenceSignalBuffer::GetDelayedReference(
    int32_t num_samples) {
    
    if (!is_initialized_) {
        return Result<ReferenceFrame>::error(
            AudioChatError::NotInitialized,
            "Reference signal buffer not initialized"
        );
    }

    if (num_samples <= 0) {
        return Result<ReferenceFrame>::error(
            AudioChatError::InvalidArgument,
            "Invalid number of samples: " + std::to_string(num_samples)
        );
    }

    int32_t samples_needed = num_samples * config_.num_channels;
    int32_t delay_samples = GetDelaySamples() * config_.num_channels;

    // We need: delay_samples + samples_needed data in buffer
    int32_t total_needed = delay_samples + samples_needed;

    if (static_cast<int32_t>(buffer_.size()) < total_needed) {
        LOG_WARN("Reference buffer underflow: have={}, need={}",
                 buffer_.size(), total_needed);
        return Result<ReferenceFrame>::error(
            AudioChatError::BufferUnderflow,
            "Insufficient buffered data: have " + std::to_string(buffer_.size()) +
            ", need " + std::to_string(total_needed)
        );
    }

    // Extract samples starting from delay_samples position
    ReferenceFrame frame;
    frame.num_samples = num_samples;
    frame.data.reserve(samples_needed);
    
    auto start_it = buffer_.begin() + delay_samples;
    auto end_it = start_it + samples_needed;
    frame.data.insert(frame.data.end(), start_it, end_it);

    frames_retrieved_++;

    return Result<ReferenceFrame>::ok(frame);
}

int32_t ReferenceSignalBuffer::GetBufferedSamples() const {
    if (config_.num_channels <= 0) {
        return 0;
    }
    return buffer_.size() / config_.num_channels;
}

std::string ReferenceSignalBuffer::GetStatistics() const {
    std::ostringstream ss;
    ss << "ReferenceSignalBuffer Statistics:\n"
       << "  Sample rate: " << config_.sample_rate_hz << " Hz\n"
       << "  Channels: " << config_.num_channels << "\n"
       << "  Frame duration: " << config_.frame_duration_ms << " ms\n"
       << "  Configured delay: " << config_.delay_ms << " ms (" 
       << GetDelaySamples() << " samples)\n"
       << "  Buffer duration: " << config_.buffer_duration_ms << " ms\n"
       << "  Max buffer size: " << GetMaxBufferSamples() * config_.num_channels << " samples\n"
       << "  Current buffer fill: " << buffer_.size() << " samples ("
       << GetBufferedSamples() << " per-channel)\n"
       << "  Frames pushed: " << frames_pushed_ << "\n"
       << "  Frames retrieved: " << frames_retrieved_;
    return ss.str();
}

void ReferenceSignalBuffer::Clear() {
    buffer_.clear();
    frames_pushed_ = 0;
    frames_retrieved_ = 0;
    LOG_INFO("Reference signal buffer cleared");
}

bool ReferenceSignalBuffer::HasSufficientData(int32_t num_samples) const {
    if (num_samples <= 0) {
        return false;
    }
    int32_t samples_needed = num_samples * config_.num_channels;
    int32_t delay_samples = GetDelaySamples() * config_.num_channels;
    return static_cast<int32_t>(buffer_.size()) >= (delay_samples + samples_needed);
}

} // namespace AudioChat
