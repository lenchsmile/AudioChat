#include "JitterBuffer.h"
#include <algorithm>
#include <stdexcept>

JitterBuffer::JitterBuffer(int frame_size,int max_size) :
	uint8_t_buffer_(nullptr),
	temp_packet_(nullptr),
	frames_per_buffer_(frame_size),
	max_size_(max_size),
	last_sequence_(0)
{
	uint8_t_buffer_ = reinterpret_cast<uint8_t*>(malloc(frames_per_buffer_ * max_size));

	if(!uint8_t_buffer_)
	{
		throw std::runtime_error("Failed to allocate jitter buffer");
	}

	for(int i = 0; i < max_size; i++)
	{
		unused_buffer_.push_back(reinterpret_cast<PPacket>(uint8_t_buffer_ + i * frames_per_buffer_));
	}
}

JitterBuffer::~JitterBuffer()
{
	stopped_ = true;
	cv.notify_all();
}

void JitterBuffer::addPacket(PPacket& packet)
{
	static std::unique_lock<std::mutex> lock(mutex,std::defer_lock);

	if(packet->sequence <= last_sequence_) return;
	lock.lock();
	if(buffer_.size() >= max_size_)
	{
		temp_packet_ = buffer_.front();
		buffer_.pop_front();
	}
	else
	{
		temp_packet_ = unused_buffer_.front();
		unused_buffer_.pop_front();
	}
	std::swap(temp_packet_,packet);
	buffer_.push_back(temp_packet_);
	lock.unlock();
	cv.notify_one();
}

void JitterBuffer::getPacket(uint8_t*& decode_buffer_ptr,int32_t& out_size)
{
	static std::unique_lock<std::mutex> lock(mutex,std::defer_lock);
	lock.lock();
	cv.wait(lock,[this]()
	{
		return !buffer_.empty() || stopped_;
	});

	std::sort(buffer_.begin(),buffer_.end(),
		[](const PPacket& a,const PPacket& b)
	{
		return a->sequence < b->sequence;
	});

	PPacket packet = buffer_.front();
	buffer_.pop_front();
	unused_buffer_.push_back(packet);
	lock.unlock();

	if(packet->sequence != last_sequence_ + 1)
	{
		last_sequence_ = packet->sequence;
		out_size = 0;
		return; // 触发PLC
	}
	last_sequence_ = packet->sequence;
	memcpy(decode_buffer_ptr,packet->payload,packet->data_size);
	out_size = packet->data_size;
	return;
}