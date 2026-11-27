#pragma once
#include <opus/opus.h>
#include <vector>

class Codec
{
public:
	Codec(int sample_rate,int channels,int frame_duration_ms = 20);
	~Codec();
	// 改为传入指针（按值），并让 out_size 为引用以便修改调用者变量
	void encode(const int16_t* pcm,uint8_t* output,int32_t in_size,int32_t& out_size);
	// 同样改为按值传指针/长度，out_size 为引用
	void decode(const uint8_t* encoded,int32_t encode_length,int16_t* decoded,int32_t max_samples_per_channel,int32_t& out_size);

	// Getters for accessing internal state
	OpusEncoder* getEncoder() { return encoder_; }
	OpusDecoder* getDecoder() { return decoder_; }
	int getFramesPerBuffer() const { return frames_per_buffer_; }

private:
	OpusEncoder* encoder_ = nullptr;
	OpusDecoder* decoder_ = nullptr;
	int frames_per_buffer_ = 0;
};
