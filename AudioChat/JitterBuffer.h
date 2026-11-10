#pragma once
#include <vector>
#include <deque>
#include <cstdint>
#include <mutex>
#include <condition_variable>

typedef struct _Packet
{
	uint64_t sequence; // 序列号
	uint64_t timestamp; // 时间戳
	uint16_t sample_rate; // 采样率
	uint16_t channels; // 通道数
	int32_t data_size; // 数据大小
	//uint16_t reserved; // 保留
	uint8_t payload[]; // 音频数据
}Packet,* PPacket;

class JitterBuffer
{
public:
	JitterBuffer(int frame_size,int max_size);
	~JitterBuffer();

	void addPacket(PPacket& packet);
	void getPacket(uint8_t*& decode_buffer_ptr,int32_t& out_size);

private:
	std::deque<PPacket> buffer_;
	std::deque<PPacket> unused_buffer_;
	uint8_t* uint8_t_buffer_;
	PPacket temp_packet_;
	int frames_per_buffer_;
	int max_size_;
	uint64_t last_sequence_;
	std::mutex mutex;
	std::condition_variable cv;
	std::atomic<bool> stopped_{false};
};
