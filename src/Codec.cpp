#include "Codec.h"

#include <stdexcept>

Codec::Codec(int sample_rate, int channels, int frame_duration_ms) {
    if ((sample_rate != 8000 && sample_rate != 16000 &&
         sample_rate != 32000 && sample_rate != 48000) ||
        channels < 1 || channels > 2 ||
        (frame_duration_ms != 10 && frame_duration_ms != 20)) {
        throw std::invalid_argument("Invalid codec configuration");
    }
    frames_per_buffer_ = sample_rate * frame_duration_ms / 1000;
    if (frames_per_buffer_ <= 0) {
        throw std::invalid_argument("Invalid codec frame size");
    }

    int error = OPUS_OK;
    encoder_ = opus_encoder_create(sample_rate, channels, OPUS_APPLICATION_VOIP, &error);
    if (error != OPUS_OK || !encoder_) {
        throw std::runtime_error(std::string("Opus encoder creation failed: ") +
                                 opus_strerror(error));
    }

    decoder_ = opus_decoder_create(sample_rate, channels, &error);
    if (error != OPUS_OK || !decoder_) {
        opus_encoder_destroy(encoder_);
        encoder_ = nullptr;
        throw std::runtime_error(std::string("Opus decoder creation failed: ") +
                                 opus_strerror(error));
    }

    opus_encoder_ctl(encoder_, OPUS_SET_BITRATE(24000));
    opus_encoder_ctl(encoder_, OPUS_SET_COMPLEXITY(5));
    opus_encoder_ctl(encoder_, OPUS_SET_INBAND_FEC(1));
    opus_encoder_ctl(encoder_, OPUS_SET_PACKET_LOSS_PERC(10));
}

Codec::~Codec() {
    if (encoder_) {
        opus_encoder_destroy(encoder_);
    }
    if (decoder_) {
        opus_decoder_destroy(decoder_);
    }
}

void Codec::decode(const uint8_t* encoded, int32_t encode_length, int16_t* decoded,
                   int32_t max_samples_per_channel, int32_t& out_size) {
    out_size = 0;
    if ((!encoded && encode_length != 0) || encode_length < 0 || !decoded ||
        max_samples_per_channel <= 0) {
        throw std::invalid_argument("Invalid Opus decode arguments");
    }

    out_size = opus_decode(decoder_, encode_length == 0 ? nullptr : encoded, encode_length,
                           decoded, max_samples_per_channel, encode_length == 0 ? 1 : 0);
    if (out_size < 0) {
        throw std::runtime_error(std::string("Opus decode failed: ") +
                                 opus_strerror(out_size));
    }
}

void Codec::encode(const int16_t* pcm, uint8_t* output, int32_t in_size, int32_t& out_size) {
    out_size = 0;
    if (!pcm || !output || in_size <= 0) {
        throw std::invalid_argument("Invalid Opus encode arguments");
    }

    out_size = opus_encode(encoder_, pcm, frames_per_buffer_, output, in_size);
    if (out_size < 0) {
        throw std::runtime_error(std::string("Opus encode failed: ") +
                                 opus_strerror(out_size));
    }
}
