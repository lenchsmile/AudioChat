#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include "JitterBuffer.h"

class NetworkHandler {
public:
    NetworkHandler(const char* local_ip, const char* remote_ip,
                   int local_port, int remote_port);
    ~NetworkHandler();

    NetworkHandler(const NetworkHandler&) = delete;
    NetworkHandler& operator=(const NetworkHandler&) = delete;

    bool send(Packet& packet);
    bool receive();
    bool getJitterBufferPacket(uint8_t* destination, size_t capacity, int32_t& out_size);
    void stop();

private:
    SOCKET sock_ = INVALID_SOCKET;
    sockaddr_in remote_addr_{};
    JitterBuffer jitter_buffer_;
    uint32_t sequence_ = 0;
    std::atomic<bool> stopped_{false};
    bool winsock_initialized_ = false;
};
