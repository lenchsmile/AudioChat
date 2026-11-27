#include "NetworkHandler.h"

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace {

void writeU16(uint8_t* destination, uint16_t value) {
    destination[0] = static_cast<uint8_t>(value >> 8);
    destination[1] = static_cast<uint8_t>(value);
}

void writeU32(uint8_t* destination, uint32_t value) {
    destination[0] = static_cast<uint8_t>(value >> 24);
    destination[1] = static_cast<uint8_t>(value >> 16);
    destination[2] = static_cast<uint8_t>(value >> 8);
    destination[3] = static_cast<uint8_t>(value);
}

void writeU64(uint8_t* destination, uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        *destination++ = static_cast<uint8_t>(value >> shift);
    }
}

uint16_t readU16(const uint8_t* source) {
    return static_cast<uint16_t>((static_cast<uint16_t>(source[0]) << 8) | source[1]);
}

uint32_t readU32(const uint8_t* source) {
    return (static_cast<uint32_t>(source[0]) << 24) |
           (static_cast<uint32_t>(source[1]) << 16) |
           (static_cast<uint32_t>(source[2]) << 8) |
           static_cast<uint32_t>(source[3]);
}

uint64_t readU64(const uint8_t* source) {
    uint64_t value = 0;
    for (int index = 0; index < 8; ++index) {
        value = (value << 8) | source[index];
    }
    return value;
}

bool validAudioFormat(uint16_t sample_rate, uint16_t channels, uint16_t frame_duration_ms) {
    return (sample_rate == 8000 || sample_rate == 16000 ||
            sample_rate == 32000 || sample_rate == 48000) &&
           (channels == 1 || channels == 2) &&
           (frame_duration_ms == 10 || frame_duration_ms == 20);
}

} // namespace

NetworkHandler::NetworkHandler(const char* local_ip, const char* remote_ip,
                               int local_port, int remote_port,
                               uint16_t expected_sample_rate, uint16_t expected_channels,
                               uint16_t expected_frame_duration_ms)
    : jitter_buffer_(64),
      expected_sample_rate_(expected_sample_rate),
      expected_channels_(expected_channels),
      expected_frame_duration_ms_(expected_frame_duration_ms) {
    if (!local_ip || !remote_ip || local_port <= 0 || local_port > 65535 ||
        remote_port <= 0 || remote_port > 65535 ||
        !validAudioFormat(expected_sample_rate, expected_channels,
                          expected_frame_duration_ms)) {
        throw std::invalid_argument("Invalid network configuration");
    }

    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        throw std::runtime_error("WSAStartup failed: " + std::to_string(WSAGetLastError()));
    }
    winsock_initialized_ = true;

    try {
        sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock_ == INVALID_SOCKET) {
            throw std::runtime_error("Socket creation failed: " +
                                     std::to_string(WSAGetLastError()));
        }

        u_long nonblocking = 1;
        if (ioctlsocket(sock_, FIONBIO, &nonblocking) == SOCKET_ERROR) {
            throw std::runtime_error("Failed to set nonblocking socket: " +
                                     std::to_string(WSAGetLastError()));
        }

        sockaddr_in local_address{};
        local_address.sin_family = AF_INET;
        local_address.sin_port = htons(static_cast<u_short>(local_port));
        if (InetPtonA(AF_INET, local_ip, &local_address.sin_addr) != 1) {
            throw std::invalid_argument("Invalid local IPv4 address");
        }
        if (bind(sock_, reinterpret_cast<const sockaddr*>(&local_address),
                 sizeof(local_address)) == SOCKET_ERROR) {
            throw std::runtime_error("Bind failed: " + std::to_string(WSAGetLastError()));
        }

        remote_addr_.sin_family = AF_INET;
        remote_addr_.sin_port = htons(static_cast<u_short>(remote_port));
        if (InetPtonA(AF_INET, remote_ip, &remote_addr_.sin_addr) != 1) {
            throw std::invalid_argument("Invalid remote IPv4 address");
        }
    } catch (...) {
        if (sock_ != INVALID_SOCKET) {
            closesocket(sock_);
            sock_ = INVALID_SOCKET;
        }
        WSACleanup();
        winsock_initialized_ = false;
        throw;
    }
}

NetworkHandler::~NetworkHandler() {
    stop();
    if (sock_ != INVALID_SOCKET) {
        closesocket(sock_);
    }
    if (winsock_initialized_) {
        WSACleanup();
    }
}

bool NetworkHandler::send(Packet& packet) {
    if (stopped_.load() || packet.data_size < 0 ||
        static_cast<size_t>(packet.data_size) > kMaxAudioPacketPayload ||
        !validAudioFormat(packet.sample_rate, packet.channels, packet.frame_duration_ms) ||
        packet.sample_rate != expected_sample_rate_ ||
        packet.channels != expected_channels_ ||
        packet.frame_duration_ms != expected_frame_duration_ms_) {
        return false;
    }

    packet.sequence = ++sequence_;
    packet.timestamp_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());

    std::array<uint8_t, kAudioPacketHeaderSize + kMaxAudioPacketPayload> wire{};
    writeU32(wire.data(), kAudioPacketMagic);
    writeU32(wire.data() + 4, packet.sequence);
    writeU64(wire.data() + 8, packet.timestamp_us);
    writeU16(wire.data() + 16, packet.sample_rate);
    writeU16(wire.data() + 18, packet.channels);
    writeU16(wire.data() + 20, packet.frame_duration_ms);
    writeU16(wire.data() + 22, 0); // Reserved for protocol v2 flags.
    writeU32(wire.data() + 24, static_cast<uint32_t>(packet.data_size));
    if (packet.data_size > 0) {
        std::memcpy(wire.data() + kAudioPacketHeaderSize, packet.payload.data(),
                    static_cast<size_t>(packet.data_size));
    }

    const int length = static_cast<int>(kAudioPacketHeaderSize + packet.data_size);
    const int sent = sendto(sock_, reinterpret_cast<const char*>(wire.data()), length, 0,
                            reinterpret_cast<const sockaddr*>(&remote_addr_),
                            sizeof(remote_addr_));
    return sent == length;
}

bool NetworkHandler::receive() {
    if (stopped_.load()) {
        return false;
    }

    std::array<uint8_t, kAudioPacketHeaderSize + kMaxAudioPacketPayload> wire{};
    sockaddr_in source{};
    int source_length = sizeof(source);
    const int length = recvfrom(sock_, reinterpret_cast<char*>(wire.data()),
                                static_cast<int>(wire.size()), 0,
                                reinterpret_cast<sockaddr*>(&source), &source_length);
    if (length == SOCKET_ERROR) {
        return false;
    }
    if (length < static_cast<int>(kAudioPacketHeaderSize)) {
        return false;
    }

    if (std::memcmp(&source.sin_addr, &remote_addr_.sin_addr, sizeof(in_addr)) != 0 ||
        source.sin_port != remote_addr_.sin_port) {
        return false;
    }

    if (readU32(wire.data()) != kAudioPacketMagic) {
        return false;
    }

    const uint32_t payload_size = readU32(wire.data() + 24);
    if (payload_size > kMaxAudioPacketPayload ||
        payload_size > static_cast<size_t>(length - kAudioPacketHeaderSize)) {
        return false;
    }

    const uint16_t sample_rate = readU16(wire.data() + 16);
    const uint16_t channels = readU16(wire.data() + 18);
    const uint16_t frame_duration_ms = readU16(wire.data() + 20);
    if (!validAudioFormat(sample_rate, channels, frame_duration_ms) ||
        sample_rate != expected_sample_rate_ || channels != expected_channels_ ||
        frame_duration_ms != expected_frame_duration_ms_) {
        return false;
    }

    Packet packet;
    packet.sequence = readU32(wire.data() + 4);
    packet.timestamp_us = readU64(wire.data() + 8);
    packet.sample_rate = sample_rate;
    packet.channels = channels;
    packet.frame_duration_ms = frame_duration_ms;
    packet.data_size = static_cast<int32_t>(payload_size);
    if (payload_size > 0) {
        std::memcpy(packet.payload.data(), wire.data() + kAudioPacketHeaderSize, payload_size);
    }
    return jitter_buffer_.addPacket(std::move(packet));
}

bool NetworkHandler::getJitterBufferPacket(uint8_t* destination, size_t capacity,
                                           int32_t& out_size) {
    return getJitterBufferPacketWithTimeout(destination, capacity, out_size, -1) ==
           JitterBuffer::Result::Packet;
}

JitterBuffer::Result NetworkHandler::getJitterBufferPacketWithTimeout(
    uint8_t* destination, size_t capacity, int32_t& out_size, int timeout_ms) {
    if (stopped_.load()) {
        out_size = 0;
        return JitterBuffer::Result::Stopped;
    }
    return jitter_buffer_.getPacketWithTimeout(destination, capacity, out_size, timeout_ms);
}

void NetworkHandler::stop() {
    stopped_.store(true);
    jitter_buffer_.stop();
}
