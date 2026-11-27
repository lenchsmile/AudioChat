#include "Result.h"

std::string getErrorDescription(AudioChatError error) {
    switch (error) {
        case AudioChatError::Success:
            return "Success";
        
        // Audio errors
        case AudioChatError::AudioDeviceNotFound:
            return "Audio device not found";
        case AudioChatError::AudioStreamOpenFailed:
            return "Failed to open audio stream";
        case AudioChatError::AudioStreamStartFailed:
            return "Failed to start audio stream";
        case AudioChatError::AudioStreamStopFailed:
            return "Failed to stop audio stream";
        case AudioChatError::AudioStreamWriteFailed:
            return "Failed to write to audio stream";
        case AudioChatError::AudioCallbackError:
            return "Audio callback error";
        case AudioChatError::InvalidAudioDevice:
            return "Invalid audio device";
        case AudioChatError::AudioDeviceAlreadyOpen:
            return "Audio device already open";
        
        // Network errors
        case AudioChatError::NetworkBindFailed:
            return "Failed to bind network socket";
        case AudioChatError::NetworkSocketCreationFailed:
            return "Failed to create network socket";
        case AudioChatError::NetworkSendFailed:
            return "Failed to send network packet";
        case AudioChatError::NetworkRecvFailed:
            return "Failed to receive network packet";
        case AudioChatError::NetworkConnectionFailed:
            return "Network connection failed";
        case AudioChatError::NetworkConnectionLost:
            return "Network connection lost";
        case AudioChatError::InvalidNetworkConfig:
            return "Invalid network configuration";
        
        // Codec errors
        case AudioChatError::CodecInitFailed:
            return "Failed to initialize codec";
        case AudioChatError::CodecEncodeFailed:
            return "Codec encoding failed";
        case AudioChatError::CodecDecodeFailed:
            return "Codec decoding failed";
        case AudioChatError::InvalidCodecConfig:
            return "Invalid codec configuration";
        
        // AEC errors
        case AudioChatError::AecInitFailed:
            return "Failed to initialize AEC";
        case AudioChatError::AecProcessFailed:
            return "AEC processing failed";
        
        // Buffer errors
        case AudioChatError::BufferAllocFailed:
            return "Buffer allocation failed";
        case AudioChatError::BufferOverflow:
            return "Buffer overflow";
        case AudioChatError::BufferUnderflow:
            return "Buffer underflow";
        
        // General errors
        case AudioChatError::InternalError:
            return "Internal error";
        case AudioChatError::InvalidArgument:
            return "Invalid argument";
        case AudioChatError::NotInitialized:
            return "Component not initialized";
        case AudioChatError::AlreadyInitialized:
            return "Component already initialized";
        
        default:
            return "Unknown error";
    }
}
