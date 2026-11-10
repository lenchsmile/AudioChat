#pragma once
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <fstream>
#include <string>
#include <atomic>

class AudioRecorder
{
public:
	AudioRecorder(int sample_rate,int channels);
	~AudioRecorder();
	void start();
	void stop();
	void recordMixed(const std::vector<int16_t>& local,const std::vector<int16_t>& remote); // 新增
	bool isRecording() const
	{
		return running_;
	} // 新增

private:
	void writeThread();
	void writeWavHeader(std::ofstream& file,int sample_rate,int channels,uint32_t data_size);
	void updateWavHeader(std::ofstream& file,uint32_t data_size);

	std::ofstream mixed_file_; // 
	std::queue<std::vector<int16_t>> mixed_queue_a_; // 缓冲队列A
	std::queue<std::vector<int16_t>> mixed_queue_b_; // 缓冲队列B
	std::atomic<int> current_queue_index; // 当前使用的队列索引 0:A, 1:B
	int current_queue_size_; // 当前队列大小
	int max_queue_size_; // 最大队列大小

	int total_mixed_data_size_; // 记录混合音频数据总大小

	std::mutex mutex_;
	std::condition_variable cv;
	std::thread write_thread_;
	std::atomic<bool> running_;
	int sample_rate_;
	int channels_;
};
