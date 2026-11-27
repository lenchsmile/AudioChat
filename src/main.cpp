#include <iostream>
#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include "NetworkHandler.h"
#include "AudioHandler.h"
#include "Codec.h"
#include <windows.h>
#include <conio.h>

static void setThreadPriority(std::thread& t)
{
	SetThreadPriority(t.native_handle(),THREAD_PRIORITY_TIME_CRITICAL);
}

// #include "AudioWidget.h"
// #include <QApplication>

int main(int argc,char** argv)
{
	// 设置系统回调时间1ms
	timeBeginPeriod(1);
	SetConsoleOutputCP(CP_UTF8);
	//SetConsoleOutputCP(CP_UTF8);
	// QApplication app(argc,argv);
	// 
	// QString info = "OK!";

	AudioHandler* audio = nullptr;

	Codec* codec = nullptr;

	NetworkHandler* network = nullptr;

	std::atomic<bool> running{true};

	try
	{
		// 读取配置文件路径（同可执行文件目录下的 config.ini）

		std::string configPath = "./config.ini";

		// 默认值
		std::string local_ip_default = "127.0.0.1";
		std::string remote_ip_default = "127.0.0.1";
		int port_default = 5000;
		int sample_rate_default = 48000;
		int frame_cache_time_default = 20; // 20ms

		// 读取IP配置
		char buf[256] = {0};
		GetPrivateProfileStringA("Network","LocalIP",local_ip_default.c_str(),buf,sizeof(buf),configPath.c_str());
		std::string local_ip_str = buf[0] ? std::string(buf) : local_ip_default;

		GetPrivateProfileStringA("Network","RemoteIP",remote_ip_default.c_str(),buf,sizeof(buf),configPath.c_str());
		std::string remote_ip_str = buf[0] ? std::string(buf) : remote_ip_default;

		// 读取整型配置
		int local_port = GetPrivateProfileIntA("Network","LocalPort",port_default,configPath.c_str());
		int remote_port = GetPrivateProfileIntA("Network","RemotePort",port_default,configPath.c_str());
		int input_sample_rate = GetPrivateProfileIntA("Audio","InputSampleRate",sample_rate_default,configPath.c_str());
		int output_sample_rate = GetPrivateProfileIntA("Audio","OutputSampleRate",sample_rate_default,configPath.c_str());
		int frame_cache_time = GetPrivateProfileIntA("Audio","FrameCacheTime",frame_cache_time_default,configPath.c_str());
		int input_frames_per_buffer = input_sample_rate * frame_cache_time / 1000;
		int output_frames_per_buffer = output_sample_rate * frame_cache_time / 1000;

		// 读取设备名称配置
		GetPrivateProfileStringA("Devices","InputDevice","",buf,sizeof(buf),configPath.c_str());
		std::string input_device_name_str = buf[0] ? std::string(buf) : "";

		GetPrivateProfileStringA("Devices","OutputDevice","",buf,sizeof(buf),configPath.c_str());
		std::string output_device_name_str = buf[0] ? std::string(buf) : "";

		HANDLE capture_ready_event = CreateEventA(NULL,FALSE,FALSE,NULL);
		if(!capture_ready_event)
		{
			throw std::runtime_error("Failed to create capture ready event");
		}

		audio = new AudioHandler(input_sample_rate,output_sample_rate,input_frames_per_buffer,output_frames_per_buffer,input_device_name_str.c_str(),output_device_name_str.c_str(),capture_ready_event);

		std::cout << "AudioHandler initialized with input sample rate: " << input_sample_rate
			<< ", output sample rate: " << output_sample_rate
			<< ", input frames per buffer: " << input_frames_per_buffer
			<< ", output frames per buffer: " << output_frames_per_buffer
			<< ", input device: " << input_device_name_str
			<< ", output device: " << output_device_name_str
			<< std::endl;

		codec = new Codec(output_sample_rate,1);

		std::cout << "Codec initialized with sample rate: " << output_sample_rate << " and channels: 1" << std::endl;

		network = new NetworkHandler(local_ip_str.c_str(),remote_ip_str.c_str(),local_port,remote_port);

		std::cout << "NetworkHandler initialized with local IP: " << local_ip_str
			<< ", remote IP: " << remote_ip_str
			<< ", local port: " << local_port
			<< ", remote port: " << remote_port
			<< std::endl;

		// 发送线程
		std::thread send_thread([&]()
		{
			int in_size{output_frames_per_buffer * 2};

			PPacket packet_ptr{reinterpret_cast<PPacket>(malloc(sizeof(Packet) + in_size * sizeof(uint8_t)))};

			if(!packet_ptr)
			{
				return;
			}

			uint8_t* encode_buffer_ptr{packet_ptr->payload};

			int16_t* capture_buffer_ptr{reinterpret_cast<int16_t*>(malloc(output_frames_per_buffer * sizeof(int16_t)))};

			if(!capture_buffer_ptr)
			{
				return;
			}

			while(running)
			{
				WaitForSingleObject(capture_ready_event,INFINITE);
				audio->swapCaptureBuffer(capture_buffer_ptr);

				codec->encode(capture_buffer_ptr,encode_buffer_ptr,in_size,packet_ptr->data_size);
				network->send(packet_ptr);
			}
		});

		// 接收线程
		std::thread recv_thread([&]()
		{
			std::vector<int16_t> decode_buffer(output_frames_per_buffer);
			int in_size{output_frames_per_buffer * 2};

			uint8_t* decode_buffer_ptr{reinterpret_cast<uint8_t*>(malloc(in_size * sizeof(uint8_t)))};

			while(running)
			{
				network->receive();
			}
		});

		// 音频播放线程
		std::thread play_thread([&]()
		{
			uint8_t* encode_buffer{reinterpret_cast<uint8_t*>(malloc(1024))};
			int32_t encode_out_size{0};
			int16_t* decode_buffer{reinterpret_cast<int16_t*>(malloc(output_frames_per_buffer * sizeof(int16_t)))};
			int32_t decode_out_size{0};

			while(running)
			{
				network->getJitterBufferPacket(encode_buffer,encode_out_size);
				codec->decode(encode_buffer,encode_out_size,decode_buffer,output_frames_per_buffer * sizeof(int16_t),decode_out_size);
				audio->play(decode_buffer,decode_out_size);
			}
		});

		// 设置线程优先级
		setThreadPriority(send_thread);
		setThreadPriority(recv_thread);
		setThreadPriority(play_thread);

		send_thread.detach();
		recv_thread.detach();
		play_thread.detach();

		std::cout << "Threads started and detached." << std::endl;

	}
	catch(const std::exception& e)
	{
		std::cerr << "Error: " << e.what() << std::endl;
		//	info = "Error: " + QString(e.what());
	}

	// std::cout << "Starting Qt application event loop." << std::endl;
	// 
	// AudioWidget widget;
	// 
	// widget.setInfoLabel(info);
	// 
	// widget.show();
	// 
	// int code = app.exec();
	// 
	// std::cout << "Exiting application with code: " << code << std::endl;
	Sleep(INFINITE);

	running = false;

	// 恢复系统回调时间
	timeEndPeriod(1);

	audio->stop();

	delete network;
	network = nullptr;
	delete codec;
	codec = nullptr;
	delete audio;
	audio = nullptr;

	return 0;
}
