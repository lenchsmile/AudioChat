#include "AudioRecorder.h"
#include <algorithm>
#include <stdexcept>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <io.h>
#include <direct.h>

AudioRecorder::AudioRecorder(int sample_rate,int channels) :
	current_queue_index(0),
	current_queue_size_(0),
	max_queue_size_(500),
	total_mixed_data_size_(0),
	sample_rate_(sample_rate),
	channels_(channels),
	running_(false)
{
	auto now = std::chrono::system_clock::now();
	std::time_t now_time = std::chrono::system_clock::to_time_t(now);

	// 格式化时间为字符串: YYYYMMDD_HHMMSS
	std::tm local_tm{};
	localtime_s(&local_tm,&now_time);

	// 使用字符串流生成文件名
	std::ostringstream oss;
	oss << "recorde/recording_" << std::put_time(&local_tm,"%Y%m%d_%H%M%S") << ".wav";
	// 生成文件名
	std::string mixed_filename = oss.str();

	if(_access_s("recorde",0))
	{
		int mkdir_result = _mkdir("recorde");
		if(mkdir_result != 0 && errno != EEXIST)
		{
			throw std::runtime_error("Failed to create 'recorde' directory");
		}
	}

	mixed_file_.open(mixed_filename,std::ios::binary);
	if(!mixed_file_.is_open())
	{
		throw std::runtime_error("Failed to open WAV files");
	}

	writeWavHeader(mixed_file_,sample_rate_,channels_,0);
}

AudioRecorder::~AudioRecorder()
{
	stop();
}

void AudioRecorder::start()
{
	running_ = true;
	write_thread_ = std::thread(&AudioRecorder::writeThread,this);
}

void AudioRecorder::stop()
{
	running_ = false;

	cv.notify_all();

	if(write_thread_.joinable())
	{
		write_thread_.join();
	}

	// 将剩余数据写入文件
	while(!mixed_queue_a_.empty())
	{
		auto& data = mixed_queue_a_.front();
		mixed_file_.write(reinterpret_cast<const char*>(data.data()),data.size() * sizeof
		(int16_t));
		total_mixed_data_size_ += static_cast<uint32_t>(data.size() * sizeof(int16_t));
		mixed_queue_a_.pop();
	}

	while(!mixed_queue_b_.empty())
	{
		auto& data = mixed_queue_b_.front();
		mixed_file_.write(reinterpret_cast<const char*>(data.data()),data.size() * sizeof
		(int16_t));
		total_mixed_data_size_ += static_cast<uint32_t>(data.size() * sizeof(int16_t));
		mixed_queue_b_.pop();
	}

	mixed_file_.flush();

	// 更新 WAV 头信息
	updateWavHeader(mixed_file_,total_mixed_data_size_);
	mixed_file_.close();
}

void AudioRecorder::recordMixed(const std::vector<int16_t>& local,const std::vector<int16_t>& remote)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if(local.size() != remote.size()) return; // 确保帧大小一致
	std::vector<int16_t> mixed(local.size());
	for(size_t i = 0; i < local.size(); ++i)
	{
		int32_t sample = (static_cast<int32_t>(local[i]) + static_cast<int32_t>(remote[i])) / 2;
		sample = std::clamp(sample, int32_t{-32768}, int32_t{32767});
		mixed[i] = static_cast<int16_t>(sample);
	}

	switch(current_queue_index)
	{
		case 0:
		{
			mixed_queue_a_.push(mixed);
			current_queue_size_++;
			if(current_queue_size_ >= max_queue_size_)
			{
				current_queue_index = 1;
				current_queue_size_ = 0;
				cv.notify_one();
			}
			break;
		}
		case 1:
		{
			mixed_queue_b_.push(mixed);
			current_queue_size_++;
			if(current_queue_size_ >= max_queue_size_)
			{
				current_queue_index = 0;
				current_queue_size_ = 0;
				cv.notify_one();
			}
			break;
		}
		default:
			break;
	}
}

void AudioRecorder::writeThread()
{
	std::unique_lock<std::mutex> lock(mutex_,std::defer_lock);

	while(running_)
	{
		std::vector<int16_t>  mixed_data;

		lock.lock();
		cv.wait(lock,[this]()
		{
			// 等待直到有数据可写入或停止运行
			return (current_queue_index ? !mixed_queue_b_.empty() : !mixed_queue_a_.empty()) || !running_;
		});
		// 选择当前非活动队列进行写入
		std::queue<std::vector<int16_t>>& mixed_queue_ = current_queue_index ? mixed_queue_b_ : mixed_queue_a_;

		if(!mixed_queue_.empty())
		{
			mixed_data = mixed_queue_.front();
			mixed_queue_.pop();
		}
		lock.unlock();

		if(!mixed_data.empty())
		{
			mixed_file_.write(reinterpret_cast<const char*>(mixed_data.data()),mixed_data.size() * sizeof(int16_t));
			total_mixed_data_size_ += static_cast<uint32_t>(mixed_data.size() * sizeof(int16_t));
		}

		mixed_file_.flush();
	}
}

void AudioRecorder::writeWavHeader(std::ofstream& file,int sample_rate,int channels,uint32_t data_size)
{
	file.seekp(0);
	file.write("RIFF",4);
	uint32_t chunk_size = 36 + data_size;
	file.write(reinterpret_cast<const char*>(&chunk_size),4);
	file.write("WAVE",4);
	file.write("fmt ",4);
	uint32_t subchunk1_size = 16;
	file.write(reinterpret_cast<const char*>(&subchunk1_size),4);
	uint16_t audio_format = 1; // PCM
	file.write(reinterpret_cast<const char*>(&audio_format),2);
	uint16_t channel_count = static_cast<uint16_t>(channels);
	file.write(reinterpret_cast<const char*>(&channel_count),2);
	uint32_t sample_rate_value = static_cast<uint32_t>(sample_rate);
	file.write(reinterpret_cast<const char*>(&sample_rate_value),4);
	uint32_t byte_rate = static_cast<uint32_t>(sample_rate * channels * 2);
	file.write(reinterpret_cast<const char*>(&byte_rate),4);
	uint16_t block_align = static_cast<uint16_t>(channels * 2);
	file.write(reinterpret_cast<const char*>(&block_align),2);
	uint16_t bits_per_sample = 16;
	file.write(reinterpret_cast<const char*>(&bits_per_sample),2);
	file.write("data",4);
	file.write(reinterpret_cast<const char*>(&data_size),4);
}

void AudioRecorder::updateWavHeader(std::ofstream& file,uint32_t data_size)
{
	file.seekp(4);
	uint32_t chunk_size = 36 + data_size;
	file.write(reinterpret_cast<const char*>(&chunk_size),4);
	file.seekp(40);
	file.write(reinterpret_cast<const char*>(&data_size),4);
}
