#pragma once
#include <portaudio.h>
#include <atomic>
#include <vector>
#include <queue>
#include <Windows.h>
#include "SpscFixedSlots.h"
/*
采用 PortAudio 进行音频捕获和播放
位深 16位整数型
声道数 单声道
采样率 48000 Hz
*/

class AudioHandler
{
public:

	/// <summary>
	/// 构造函数，初始化音频处理器
	/// </summary>
	/// <param name="input_sample_rate"></param>
	/// <param name="output_sample_rate"></param>
	/// <param name="input_frames_per_buffer"></param>
	/// <param name="output_frames_per_buffer"></param>
	/// <param name="input_name"></param>
	/// <param name="output_name"></param>
	/// <param name="capture_ready_event"></param>
	AudioHandler(int input_sample_rate,int output_sample_rate,int input_frames_per_buffer,int output_frames_per_buffer,const char* input_name,const char* output_name,HANDLE capture_ready_event);

	/// <summary>
	/// 构造函数，初始化音频处理器
	/// </summary>
	/// <param name="frame_cache_time"></param>
	/// <param name="input_name"></param>
	/// <param name="output_name"></param>
	/// <param name="capture_ready_even"></param>
	AudioHandler(int frame_cache_time,const char* input_name,const char* output_name,HANDLE capture_ready_even);

	// 禁用拷贝和移动构造函数及赋值运算符
	AudioHandler(const AudioHandler&) = delete;
	AudioHandler(AudioHandler&&) = delete;
	AudioHandler& operator=(const AudioHandler&) = delete;
	AudioHandler& operator=(AudioHandler&&) = delete;


	/// <summary>
	/// 析构函数，释放资源，停止音频处理。
	/// </summary>
	~AudioHandler();

	/// <summary>
	///	交换捕获缓冲区指针
	/// </summary>
	/// <param name="capture_buffer_ptr">指向交换缓冲的指针</param>
	void swapCaptureBuffer(int16_t*& capture_buffer_ptr);

	/// <summary>
	///  开始录制音频
	/// </summary>
	void startRecordeAudio();

	/// <summary>
	///  停止录制音频
	/// </summary>
	void stopRecordeAudio();

	/// <summary>
	/// 是否正在录制音频
	/// </summary>
	/// <returns></returns>
	bool isRecording();

	/// <summary>
	///	播放音频数据	
	/// </summary>
	/// <param name="data">播放的数据</param>
	/// <param name="data_size">数据大小（字节）</param>
	void play(const int16_t* data,int32_t data_size);

	/// <summary>
	///  停止音频处理器
	/// </summary>
	void stop();

private:
	// 
	void selectValidDevices();

	/// <summary>
	///  
	/// </summary>
	/// <param name="input"></param>
	/// <param name="output"></param>
	/// <param name="frameCount"></param>
	/// <param name="timeInfo"></param>
	/// <param name="statusFlags"></param>
	/// <param name="userData"></param>
	/// <returns></returns>
	static int audioCallback(const void* input,void* output,unsigned long frameCount,
		const PaStreamCallbackTimeInfo* timeInfo,PaStreamCallbackFlags statusFlags,void* userData);

	// 是否正在录制
	std::atomic<bool> recording;

	SpscFixedSlots<int16_t> capture_queue_;	// 捕获队列	
	SpscFixedSlots<int16_t> playback_queue_;	// 播放队列

	HANDLE capture_ready_event_;
	char* input_device_name;
	char* output_device_name;

	int input_sample_rate_;// 音频输入采样率
	int output_sample_rate_;// 音频输出采样率
	int input_frames_per_buffer_;// 每个音频输入回调缓冲包含的帧数
	int output_frames_per_buffer_;// 每个音频输出回调缓冲包含的帧数

	PaDeviceIndex input_device;
	PaDeviceIndex output_device;
	PaStream* input_stream_;
	PaStream* output_stream_;

	int16_t* capture_buffer_ptr_;
};
