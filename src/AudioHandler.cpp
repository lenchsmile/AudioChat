#include "AudioHandler.h"
#include <stdexcept>
#include <cstring>
#include <iostream>
#include <thread>
#include "AudioRecorder.h"

AudioHandler::AudioHandler(int input_sample_rate,int output_sample_rate,int input_frames_per_buffer,int output_frames_per_buffer,const char* input_name,const char* output_name,HANDLE capture_ready_event) :
	recording(false),
	capture_queue_(4096,output_frames_per_buffer),
	playback_queue_(4096,output_frames_per_buffer),
	capture_ready_event_(capture_ready_event),
	input_device_name(_strdup(input_name)),
	output_device_name(_strdup(output_name)),

	input_sample_rate_(input_sample_rate),
	output_sample_rate_(output_sample_rate),
	input_frames_per_buffer_(input_frames_per_buffer),
	output_frames_per_buffer_(output_frames_per_buffer),

	input_device(paNoDevice),
	output_device(paNoDevice),
	capture_buffer_ptr_(nullptr)
{
	PaError err = Pa_Initialize();
	if(err != paNoError)
	{
		throw std::runtime_error(std::string("Pa_Initialize failed: ") + Pa_GetErrorText(err));
	}

	selectValidDevices();

	capture_buffer_ptr_ = reinterpret_cast<int16_t*>(malloc(output_frames_per_buffer * sizeof(int16_t)));

	if(!capture_buffer_ptr_)
	{
		throw std::runtime_error("Failed to allocate capture buffer");
	}

	PaStreamParameters input_params{};
	input_params.device = input_device;
	if(input_params.device == paNoDevice) throw std::runtime_error("No input device");
	input_params.channelCount = 1;
	input_params.sampleFormat = paInt16;
	input_params.suggestedLatency = Pa_GetDeviceInfo(input_params.device)->defaultLowInputLatency;
	input_params.hostApiSpecificStreamInfo = nullptr;

	PaStreamParameters output_params{};
	output_params.device = output_device;
	if(output_params.device == paNoDevice) throw std::runtime_error("No output device");
	output_params.channelCount = 1;
	output_params.sampleFormat = paInt16;
	output_params.suggestedLatency = Pa_GetDeviceInfo(output_params.device)->defaultLowOutputLatency;
	output_params.hostApiSpecificStreamInfo = nullptr;

	err = Pa_OpenStream(&input_stream_,&input_params,nullptr,input_sample_rate_,input_frames_per_buffer_,paClipOff,audioCallback,this);
	if(err != paNoError) throw std::runtime_error(std::string("Pa_OpenStream (input) failed: ") + Pa_GetErrorText(err));

	err = Pa_OpenStream(&output_stream_,nullptr,&output_params,output_sample_rate_,output_frames_per_buffer_,paClipOff,nullptr,nullptr);
	if(err != paNoError) throw std::runtime_error(std::string("Pa_OpenStream (output) failed: ") + Pa_GetErrorText(err));

	err = Pa_StartStream(input_stream_);
	if(err != paNoError) throw std::runtime_error(std::string("Pa_StartStream (input) failed: ") + Pa_GetErrorText(err));

	err = Pa_StartStream(output_stream_);
	if(err != paNoError) throw std::runtime_error(std::string("Pa_StartStream (output) failed: ") + Pa_GetErrorText(err));
}

AudioHandler::AudioHandler(int frame_cache_time,const char* input_name,const char* output_name,HANDLE capture_ready_even) :
	AudioHandler(48000,48000,48000 * frame_cache_time / 1000,48000 * frame_cache_time / 1000,input_name,output_name,capture_ready_even)
{
}

AudioHandler::~AudioHandler()
{
	stop();
	Pa_Terminate();
	free(input_device_name);
	free(output_device_name);
}

int AudioHandler::audioCallback(const void* input,void* output,unsigned long frameCount,
	const PaStreamCallbackTimeInfo* timeInfo,PaStreamCallbackFlags statusFlags,void* userData)
{
	static AudioHandler* handler = static_cast<AudioHandler*>(userData);


	memcpy(handler->capture_buffer_ptr_,input,frameCount * sizeof(int16_t));

	SetEvent(handler->capture_ready_event_);
	return paContinue;
}

void AudioHandler::play(const int16_t* data,int32_t data_size)
{
	if(recording)
	{
		playback_queue_.tryWriteFrom(data);
	}
	PaError err = Pa_WriteStream(output_stream_,data,data_size);
	if(err != paNoError)
	{
		std::cerr << "Pa_WriteStream failed: " << Pa_GetErrorText(err) << std::endl;
	}
}

void AudioHandler::swapCaptureBuffer(int16_t*& capture_buffer_ptr)
{
	// if(recording)
	// {
	// 	capture_queue_.tryWriteFrom(capture_buffer_ptr_);
	// }
	std::swap(capture_buffer_ptr,capture_buffer_ptr_);
}

void AudioHandler::startRecordeAudio()
{
	recording = true;
	std::thread([this]()
	{
		AudioRecorder recorder(output_sample_rate_,1);

		int16_t* capture_ptr{nullptr};
		int16_t* playback_ptr{nullptr};

		thread_local int16_t* local_capture_ptr{reinterpret_cast<int16_t*>(malloc(output_frames_per_buffer_ * sizeof(int16_t)))};

		if(!local_capture_ptr)
		{
			return;
		}

		thread_local int16_t* local_playback_ptr{reinterpret_cast<int16_t*>(malloc(output_frames_per_buffer_ * sizeof(int16_t)))};

		if(!local_playback_ptr)
		{
			return;
		}

		recorder.start();
		while(recording)
		{
			capture_ptr = capture_queue_.tryAcquireRead();

			if(capture_ptr)
			{
				memcpy(local_capture_ptr,capture_ptr,output_frames_per_buffer_ * sizeof(int16_t));
				capture_queue_.commitRead();
			}


			playback_ptr = playback_queue_.tryAcquireRead();

			if(playback_ptr)
			{
				memcpy(local_playback_ptr,playback_ptr,output_frames_per_buffer_ * sizeof(int16_t));
				playback_queue_.commitRead();
			}

			if(capture_ptr && playback_ptr)
			{
				recorder.recordMixed(std::vector<int16_t>(local_capture_ptr,local_capture_ptr + output_frames_per_buffer_),std::vector<int16_t>(local_playback_ptr,local_playback_ptr + output_frames_per_buffer_));
			}
			else
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
		}
	}).detach();
}

void AudioHandler::stopRecordeAudio()
{
	recording = false;
}

bool AudioHandler::isRecording()
{
	return recording;
}

void AudioHandler::stop()
{
	if(input_stream_)
	{
		Pa_StopStream(input_stream_);
		input_stream_ = nullptr;
	}
	if(output_stream_)
	{
		Pa_CloseStream(output_stream_);
		output_stream_ = nullptr;
	}
}

void AudioHandler::selectValidDevices()
{
	int num_devices = Pa_GetDeviceCount();

	if(num_devices < 0)
	{
		std::cerr << "Pa_GetDeviceCount error: " << Pa_GetErrorText(num_devices) << "\n";
		Pa_Terminate();
		return;
	}
	PaDeviceIndex defaultInput = Pa_GetDefaultInputDevice();

	for(int i = 0; i < num_devices; ++i)
	{
		const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
		const PaHostApiInfo* hostInfo = Pa_GetHostApiInfo(info->hostApi);

		if(hostInfo->type != paWASAPI)
		{
			continue;
		}

		//if (strcmp(info->name, u8"扬声器 (Realtek(R) Audio)"))
		if(!strcmp(info->name,input_device_name))
		{
			input_device = i;
			continue;
		}

		//if (strcmp(info->name, u8"Microphone Array (适用于数字麦克风的英特尔® 智音技术)"))
		if(!strcmp(info->name,output_device_name))
		{
			output_device = i;
			continue;
		}
	}
}
