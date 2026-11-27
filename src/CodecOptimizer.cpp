#include "CodecOptimizer.h"
#include "Codec.h"
#include "Logger.h"
#include <opus/opus.h>
#include <sstream>

namespace AudioChat {

// ============================================================================
// CodecOptimizationConfig Implementation
// ============================================================================

Result<void> CodecOptimizationConfig::Validate() const {
    // Validate bitrate
    if (bitrate_bps < 8000 || bitrate_bps > 128000) {
        return Result<void>::error(
            AudioChatError::InvalidCodecConfig,
            "Invalid bitrate: " + std::to_string(bitrate_bps) + 
            " bps (must be 8000-128000)"
        );
    }

    // Validate min/max bitrates
    if (min_bitrate_bps < 8000 || min_bitrate_bps > 128000) {
        return Result<void>::error(
            AudioChatError::InvalidCodecConfig,
            "Invalid min bitrate: " + std::to_string(min_bitrate_bps)
        );
    }

    if (max_bitrate_bps < 8000 || max_bitrate_bps > 128000) {
        return Result<void>::error(
            AudioChatError::InvalidCodecConfig,
            "Invalid max bitrate: " + std::to_string(max_bitrate_bps)
        );
    }

    // Min must be <= target <= max
    if (min_bitrate_bps > bitrate_bps || bitrate_bps > max_bitrate_bps) {
        return Result<void>::error(
            AudioChatError::InvalidCodecConfig,
            "Bitrate not in range [" + std::to_string(min_bitrate_bps) + 
            ", " + std::to_string(max_bitrate_bps) + "]"
        );
    }

    // Validate complexity
    if (complexity < 0 || complexity > 10) {
        return Result<void>::error(
            AudioChatError::InvalidCodecConfig,
            "Invalid complexity: " + std::to_string(complexity) + 
            " (must be 0-10)"
        );
    }

    // Validate packet loss
    if (packet_loss_percent < 0 || packet_loss_percent > 100) {
        return Result<void>::error(
            AudioChatError::InvalidCodecConfig,
            "Invalid packet loss: " + std::to_string(packet_loss_percent) + "%"
        );
    }

    return Result<void>::ok();
}

// ============================================================================
// CodecOptimizer Implementation
// ============================================================================

Result<std::unique_ptr<CodecOptimizer>> CodecOptimizer::Create(
    Codec* codec,
    const CodecOptimizationConfig& config) {
    
    if (!codec) {
        return Result<std::unique_ptr<CodecOptimizer>>::error(
            AudioChatError::InvalidArgument,
            "Codec is null"
        );
    }

    // Validate config
    auto validation = config.Validate();
    if (validation.isError()) {
        LOG_ERROR("CodecOptimizer::Create: Config validation failed - {}", 
                  validation.message());
        return Result<std::unique_ptr<CodecOptimizer>>::error(
            validation.error(),
            validation.message()
        );
    }

    // Create optimizer
    auto optimizer = std::unique_ptr<CodecOptimizer>(
        new CodecOptimizer(codec, config)
    );

    // Apply configuration
    auto apply_result = optimizer->ApplyConfiguration();
    if (apply_result.isError()) {
        LOG_ERROR("CodecOptimizer::Create: Failed to apply config - {}", 
                  apply_result.message());
        return Result<std::unique_ptr<CodecOptimizer>>::error(
            apply_result.error(),
            apply_result.message()
        );
    }

    optimizer->is_initialized_ = true;
    return Result<std::unique_ptr<CodecOptimizer>>::ok(optimizer.release());
}

CodecOptimizer::CodecOptimizer(Codec* codec, const CodecOptimizationConfig& config)
    : config_(config), codec_(codec) {
    LOG_INFO("CodecOptimizer created: bitrate={} bps, DTX={}, VBR={}, complexity={}",
             config.bitrate_bps, config.enable_dtx, config.enable_vbr, config.complexity);
}

CodecOptimizer::~CodecOptimizer() {
    LOG_INFO("CodecOptimizer destroyed: {} settings applied, {} bitrate changes",
             settings_applied_, bitrate_changes_);
}

Result<void> CodecOptimizer::ApplyConfiguration() {
    if (!codec_) {
        return Result<void>::error(
            AudioChatError::NotInitialized,
            "Codec not available"
        );
    }

    OpusEncoder* encoder = codec_->getEncoder();
    if (!encoder) {
        return Result<void>::error(
            AudioChatError::CodecInitFailed,
            "Encoder is null"
        );
    }

    int err = OPUS_OK;

    // Set bitrate
    err = opus_encoder_ctl(encoder, OPUS_SET_BITRATE(config_.bitrate_bps));
    if (err != OPUS_OK) {
        LOG_WARN("Failed to set bitrate: {}", opus_strerror(err));
        // Don't fail - continue with other settings
    } else {
        settings_applied_++;
    }

    // Set DTX (Discontinuous Transmission)
    err = opus_encoder_ctl(encoder, OPUS_SET_DTX(config_.enable_dtx ? 1 : 0));
    if (err != OPUS_OK) {
        LOG_WARN("Failed to set DTX: {}", opus_strerror(err));
    } else {
        settings_applied_++;
        LOG_INFO("DTX {}", config_.enable_dtx ? "enabled" : "disabled");
    }

    // Set VBR (Variable Bitrate)
    err = opus_encoder_ctl(encoder, OPUS_SET_VBR(config_.enable_vbr ? 1 : 0));
    if (err != OPUS_OK) {
        LOG_WARN("Failed to set VBR: {}", opus_strerror(err));
    } else {
        settings_applied_++;
        LOG_INFO("VBR {}", config_.enable_vbr ? "enabled" : "disabled");
    }

    // Set VBR constraint (if VBR enabled)
    if (config_.enable_vbr) {
        err = opus_encoder_ctl(encoder, OPUS_SET_VBR_CONSTRAINT(
            config_.enable_vbr_constraint ? 1 : 0
        ));
        if (err != OPUS_OK) {
            LOG_WARN("Failed to set VBR constraint: {}", opus_strerror(err));
        } else {
            settings_applied_++;
        }
    }

    // Set complexity
    err = opus_encoder_ctl(encoder, OPUS_SET_COMPLEXITY(config_.complexity));
    if (err != OPUS_OK) {
        LOG_WARN("Failed to set complexity: {}", opus_strerror(err));
    } else {
        settings_applied_++;
    }

    // Set FEC (Forward Error Correction)
    err = opus_encoder_ctl(encoder, OPUS_SET_INBAND_FEC(config_.enable_fec ? 1 : 0));
    if (err != OPUS_OK) {
        LOG_WARN("Failed to set FEC: {}", opus_strerror(err));
    } else {
        settings_applied_++;
        LOG_INFO("FEC {}", config_.enable_fec ? "enabled" : "disabled");
    }

    // Set packet loss expectation
    err = opus_encoder_ctl(encoder, OPUS_SET_PACKET_LOSS_PERC(config_.packet_loss_percent));
    if (err != OPUS_OK) {
        LOG_WARN("Failed to set packet loss: {}", opus_strerror(err));
    } else {
        settings_applied_++;
    }

    LOG_INFO("CodecOptimizer: Applied {} settings", settings_applied_);
    return Result<void>::ok();
}

Result<void> CodecOptimizer::SetDtxEnabled(bool enable) {
    if (!codec_) {
        return Result<void>::error(
            AudioChatError::NotInitialized,
            "Codec not available"
        );
    }

    int err = opus_encoder_ctl(codec_->getEncoder(), 
                               OPUS_SET_DTX(enable ? 1 : 0));
    if (err != OPUS_OK) {
        LOG_ERROR("Failed to set DTX: {}", opus_strerror(err));
        return Result<void>::error(
            AudioChatError::CodecInitFailed,
            std::string("Failed to set DTX: ") + opus_strerror(err)
        );
    }

    config_.enable_dtx = enable;
    settings_applied_++;
    LOG_INFO("DTX {}", enable ? "enabled" : "disabled");
    return Result<void>::ok();
}

Result<void> CodecOptimizer::SetBitrate(int32_t bitrate_bps) {
    if (bitrate_bps < 8000 || bitrate_bps > 128000) {
        return Result<void>::error(
            AudioChatError::InvalidArgument,
            "Invalid bitrate: " + std::to_string(bitrate_bps) + 
            " bps (must be 8000-128000)"
        );
    }

    if (!codec_) {
        return Result<void>::error(
            AudioChatError::NotInitialized,
            "Codec not available"
        );
    }

    int err = opus_encoder_ctl(codec_->getEncoder(), 
                               OPUS_SET_BITRATE(bitrate_bps));
    if (err != OPUS_OK) {
        LOG_ERROR("Failed to set bitrate: {}", opus_strerror(err));
        return Result<void>::error(
            AudioChatError::CodecInitFailed,
            std::string("Failed to set bitrate: ") + opus_strerror(err)
        );
    }

    config_.bitrate_bps = bitrate_bps;
    bitrate_changes_++;
    LOG_INFO("Bitrate changed to {} bps", bitrate_bps);
    return Result<void>::ok();
}

Result<void> CodecOptimizer::SetVbrEnabled(bool enable) {
    if (!codec_) {
        return Result<void>::error(
            AudioChatError::NotInitialized,
            "Codec not available"
        );
    }

    int err = opus_encoder_ctl(codec_->getEncoder(), 
                               OPUS_SET_VBR(enable ? 1 : 0));
    if (err != OPUS_OK) {
        LOG_ERROR("Failed to set VBR: {}", opus_strerror(err));
        return Result<void>::error(
            AudioChatError::CodecInitFailed,
            std::string("Failed to set VBR: ") + opus_strerror(err)
        );
    }

    config_.enable_vbr = enable;
    settings_applied_++;
    LOG_INFO("VBR {}", enable ? "enabled" : "disabled");
    return Result<void>::ok();
}

Result<void> CodecOptimizer::SetComplexity(int32_t complexity) {
    if (complexity < 0 || complexity > 10) {
        return Result<void>::error(
            AudioChatError::InvalidArgument,
            "Invalid complexity: " + std::to_string(complexity) + 
            " (must be 0-10)"
        );
    }

    if (!codec_) {
        return Result<void>::error(
            AudioChatError::NotInitialized,
            "Codec not available"
        );
    }

    int err = opus_encoder_ctl(codec_->getEncoder(), 
                               OPUS_SET_COMPLEXITY(complexity));
    if (err != OPUS_OK) {
        LOG_ERROR("Failed to set complexity: {}", opus_strerror(err));
        return Result<void>::error(
            AudioChatError::CodecInitFailed,
            std::string("Failed to set complexity: ") + opus_strerror(err)
        );
    }

    config_.complexity = complexity;
    settings_applied_++;
    LOG_INFO("Complexity set to {}", complexity);
    return Result<void>::ok();
}

Result<void> CodecOptimizer::SetPacketLossPercent(int32_t loss_percent) {
    if (loss_percent < 0 || loss_percent > 100) {
        return Result<void>::error(
            AudioChatError::InvalidArgument,
            "Invalid packet loss: " + std::to_string(loss_percent) + "%"
        );
    }

    if (!codec_) {
        return Result<void>::error(
            AudioChatError::NotInitialized,
            "Codec not available"
        );
    }

    int err = opus_encoder_ctl(codec_->getEncoder(), 
                               OPUS_SET_PACKET_LOSS_PERC(loss_percent));
    if (err != OPUS_OK) {
        LOG_ERROR("Failed to set packet loss: {}", opus_strerror(err));
        return Result<void>::error(
            AudioChatError::CodecInitFailed,
            std::string("Failed to set packet loss: ") + opus_strerror(err)
        );
    }

    config_.packet_loss_percent = loss_percent;
    settings_applied_++;
    LOG_INFO("Packet loss expectation set to {}%", loss_percent);
    return Result<void>::ok();
}

std::string CodecOptimizer::GetStatistics() const {
    std::ostringstream ss;
    ss << "CodecOptimizer Statistics:\n"
       << "  Bitrate: " << config_.bitrate_bps << " bps\n"
       << "  DTX: " << (config_.enable_dtx ? "enabled" : "disabled") << "\n"
       << "  VBR: " << (config_.enable_vbr ? "enabled" : "disabled") << "\n"
       << "  VBR Constraint: " << (config_.enable_vbr_constraint ? "yes" : "no") << "\n"
       << "  FEC: " << (config_.enable_fec ? "enabled" : "disabled") << "\n"
       << "  Complexity: " << config_.complexity << "\n"
       << "  Packet Loss: " << config_.packet_loss_percent << "%\n"
       << "  Adaptive Bitrate: " << (config_.enable_adaptive_bitrate ? "enabled" : "disabled") << "\n"
       << "  Bitrate Range: [" << config_.min_bitrate_bps << ", " 
       << config_.max_bitrate_bps << "] bps\n"
       << "  Settings Applied: " << settings_applied_ << "\n"
       << "  Bitrate Changes: " << bitrate_changes_;
    return ss.str();
}

} // namespace AudioChat
