#pragma once
#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <string>
#include <fstream>
#include "JitterBuffer.h"

class NetworkHandler
{
public:
	NetworkHandler(const char* local_ip,const char* remote_ip,int local_port,int remote_port);
	~NetworkHandler();
	void send(PPacket packet_ptr);
	void receive();
	void getJitterBufferPacket(uint8_t*& decode_buffer_ptr,int32_t& out_size);
private:

	SOCKET sock_;
	sockaddr_in remote_addr_;
	PPacket packet_ptr_;
	uint8_t* recv_buffer_;
	int32_t recv_buffer_size_;
	JitterBuffer jitter_buffer_;
	uint32_t sequence_;
};