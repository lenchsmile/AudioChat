#include "NetworkHandler.h"
#include <stdexcept>
#include <chrono>

NetworkHandler::NetworkHandler(const char* local_ip,const char* remote_ip,int local_port,int remote_port) :
	recv_buffer_size_(1920),
	jitter_buffer_(recv_buffer_size_,3),
	sequence_(0)
{
	WSADATA wsaData;
	if(WSAStartup(MAKEWORD(2,2),&wsaData) != 0)
	{
		throw std::runtime_error("WSAStartup failed: " + std::to_string(WSAGetLastError()));
	}

	sock_ = socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
	if(sock_ == INVALID_SOCKET)
	{
		throw std::runtime_error("Socket creation failed: " + std::to_string(WSAGetLastError()));
	}

	u_long mode = 1;
	/* if (ioctlsocket(sock_, FIONBIO, &mode) == SOCKET_ERROR) {
		 throw std::runtime_error("Set non-blocking failed: " + std::to_string(WSAGetLastError()));
	 }*/

	sockaddr_in local_addr;
	local_addr.sin_family = AF_INET;
	local_addr.sin_port = htons(local_port);
	inet_pton(AF_INET,local_ip,&local_addr.sin_addr);
	if(bind(sock_,(sockaddr*)&local_addr,sizeof(local_addr)) == SOCKET_ERROR)
	{
		throw std::runtime_error("Bind failed: " + std::to_string(WSAGetLastError()));
	}

	remote_addr_.sin_family = AF_INET;
	remote_addr_.sin_port = htons(remote_port);
	inet_pton(AF_INET,remote_ip,&remote_addr_.sin_addr);

	recv_buffer_ = reinterpret_cast<uint8_t*>(malloc(recv_buffer_size_));

	if(!recv_buffer_)
	{
		throw std::runtime_error("Failed to allocate receive buffer");
	}

	packet_ptr_ = reinterpret_cast<PPacket>(recv_buffer_);

}

NetworkHandler::~NetworkHandler()
{
	closesocket(sock_);
	WSACleanup();
}

void NetworkHandler::send(PPacket packet_ptr)
{
	packet_ptr->sequence = ++sequence_;
	packet_ptr->timestamp = std::chrono::system_clock::now().time_since_epoch().count();

	sendto(sock_,(const char*)packet_ptr,packet_ptr->data_size + sizeof(Packet),0,
		(sockaddr*)&remote_addr_,sizeof(remote_addr_));

	/*if (sendto(sock_, (const char*)packet_ptr, packet_ptr->data_size + sizeof(Packet), 0,
		(sockaddr*)&remote_addr_, sizeof(remote_addr_)) == SOCKET_ERROR)
	{
		log_file_ << "Send failed: " << WSAGetLastError() << "\n";
	}
	else
	{
		log_file_ << "Sent packet: " << sequence_ << "\n";
	}*/
}

void NetworkHandler::receive()
{
	int len = recv(sock_,(char*)packet_ptr_,recv_buffer_size_,0);
	if(len <= 0)
	{
		/*if (WSAGetLastError() != WSAEWOULDBLOCK)
		{
			log_file_ << "Receive failed: " << WSAGetLastError() << "\n";
		}*/
		return;
	}

	jitter_buffer_.addPacket(packet_ptr_);

}

void NetworkHandler::getJitterBufferPacket(uint8_t*& decode_buffer_ptr,int32_t& out_size)
{
	jitter_buffer_.getPacket(decode_buffer_ptr,out_size);
}
