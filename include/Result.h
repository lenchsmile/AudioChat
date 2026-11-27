#pragma once

#include <string>
#include <optional>
#include <stdexcept>

/**
 * @brief Audio Chat error codes
 */
enum class AudioChatError {
    Success = 0,
    // Audio errors
    AudioDeviceNotFound,
    AudioStreamOpenFailed,
    AudioStreamStartFailed,
    AudioStreamStopFailed,
    AudioStreamWriteFailed,
    AudioCallbackError,
    InvalidAudioDevice,
    AudioDeviceAlreadyOpen,
    
    // Network errors
    NetworkBindFailed,
    NetworkSocketCreationFailed,
    NetworkSendFailed,
    NetworkRecvFailed,
    NetworkConnectionFailed,
    NetworkConnectionLost,
    InvalidNetworkConfig,
    
    // Codec errors
    CodecInitFailed,
    CodecEncodeFailed,
    CodecDecodeFailed,
    InvalidCodecConfig,
    
    // AEC errors
    AecInitFailed,
    AecProcessFailed,
    
    // Buffer errors
    BufferAllocFailed,
    BufferOverflow,
    BufferUnderflow,
    
    // General errors
    InternalError,
    InvalidArgument,
    NotInitialized,
    AlreadyInitialized,
};

/**
 * @brief Result class for error handling using Result type pattern
 */
template <typename T = void>
class Result {
public:
    /**
     * @brief Create a successful result
     */
    static Result ok(const T& value = T()) {
        return Result(value, AudioChatError::Success, "");
    }

    /**
     * @brief Create an error result
     */
    static Result error(AudioChatError error_code, const std::string& message = "") {
        return Result(T(), error_code, message);
    }

    /**
     * @brief Check if result is successful
     */
    bool isOk() const { return error_code_ == AudioChatError::Success; }

    /**
     * @brief Check if result is an error
     */
    bool isError() const { return error_code_ != AudioChatError::Success; }

    /**
     * @brief Get the error code
     */
    AudioChatError error() const { return error_code_; }

    /**
     * @brief Get the error message
     */
    const std::string& message() const { return message_; }

    /**
     * @brief Get the value (only valid if isOk())
     */
    const T& value() const { return value_; }

    /**
     * @brief Get mutable reference to value
     */
    T& value() { return value_; }

    /**
     * @brief Convert to string for logging
     */
    std::string toString() const {
        return "Result{code=" + std::to_string(static_cast<int>(error_code_)) +
               ", message=" + message_ + "}";
    }

    /**
     * @brief Throw exception if error
     */
    void throwIfError() const {
        if (isError()) {
            throw std::runtime_error(message_);
        }
    }

private:
    T value_;
    AudioChatError error_code_;
    std::string message_;

    explicit Result(const T& value, AudioChatError error_code, const std::string& message)
        : value_(value), error_code_(error_code), message_(message) {}
};

/**
 * @brief Specialization for void result
 */
template <>
class Result<void> {
public:
    static Result ok() {
        return Result(AudioChatError::Success, "");
    }

    static Result error(AudioChatError error_code, const std::string& message = "") {
        return Result(error_code, message);
    }

    bool isOk() const { return error_code_ == AudioChatError::Success; }

    bool isError() const { return error_code_ != AudioChatError::Success; }

    AudioChatError error() const { return error_code_; }

    const std::string& message() const { return message_; }

    std::string toString() const {
        return "Result{code=" + std::to_string(static_cast<int>(error_code_)) +
               ", message=" + message_ + "}";
    }

    void throwIfError() const {
        if (isError()) {
            throw std::runtime_error(message_);
        }
    }

private:
    AudioChatError error_code_;
    std::string message_;

    explicit Result(AudioChatError error_code, const std::string& message)
        : error_code_(error_code), message_(message) {}
};

/**
 * @brief Utility function to get error code description
 */
std::string getErrorDescription(AudioChatError error);
