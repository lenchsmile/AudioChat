#include "Codec.h"
#include <stdexcept>

Codec::Codec(int sample_rate,int channels) :
	frames_per_buffer_(sample_rate / 50)
{
	int err;
	encoder_ = opus_encoder_create(sample_rate,channels,OPUS_APPLICATION_VOIP,&err);
	if(err != OPUS_OK) throw std::runtime_error(std::string("Opus encoder creation failed: ") + opus_strerror(err));
	opus_encoder_ctl(encoder_,OPUS_SET_BITRATE(24000));
	opus_encoder_ctl(encoder_,OPUS_SET_COMPLEXITY(5));
	opus_encoder_ctl(encoder_,OPUS_SET_INBAND_FEC(1));
	opus_encoder_ctl(encoder_,OPUS_SET_PACKET_LOSS_PERC(10));

	decoder_ = opus_decoder_create(sample_rate,channels,&err);
	if(err != OPUS_OK) throw std::runtime_error(std::string("Opus decoder creation failed: ") + opus_strerror(err));

}

void Codec::decode(const uint8_t* encoded,int32_t encode_length,int16_t* decoded,int32_t decode_length,int32_t& out_size)
{
	out_size = opus_decode(decoder_,encode_length == 0 ? nullptr : encoded,encode_length,
		decoded,decode_length,encode_length == 0 ? 1 : 0);
	if(out_size < 0) throw std::runtime_error(std::string("Opus decode failed: ") + opus_strerror(out_size));
}

Codec::~Codec()
{
	opus_encoder_destroy(encoder_);
	opus_decoder_destroy(decoder_);
}

void Codec::encode(const int16_t* pcm,uint8_t* output,int32_t in_size,int32_t& out_size)
{
	out_size = opus_encode(encoder_,pcm,frames_per_buffer_,output,in_size);
	if(out_size < 0) throw std::runtime_error(std::string("Opus encode failed: ") + opus_strerror(out_size));
}
