#include <gtest/gtest.h>
#include "NetworkHandler.h"

#include <array>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;

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

SOCKET createBoundUdpSocket(int port) {
    SOCKET socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_handle == INVALID_SOCKET) {
        throw std::runtime_error("socket failed");
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<u_short>(port));
    if (InetPtonA(AF_INET, "127.0.0.1", &address.sin_addr) != 1 ||
        bind(socket_handle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        closesocket(socket_handle);
        throw std::runtime_error("bind failed");
    }
    return socket_handle;
}

void sendRaw(SOCKET source, int destination_port, const std::array<uint8_t, 24>& header,
             const uint8_t* payload, size_t payload_size) {
    std::array<uint8_t, kAudioPacketHeaderSize + kMaxAudioPacketPayload> wire{};
    std::memcpy(wire.data(), header.data(), header.size());
    if (payload && payload_size > 0) {
        std::memcpy(wire.data() + kAudioPacketHeaderSize, payload, payload_size);
    }
    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(static_cast<u_short>(destination_port));
    InetPtonA(AF_INET, "127.0.0.1", &destination.sin_addr);
    const int sent = sendto(source, reinterpret_cast<const char*>(wire.data()),
                            static_cast<int>(kAudioPacketHeaderSize + payload_size), 0,
                            reinterpret_cast<sockaddr*>(&destination), sizeof(destination));
    ASSERT_NE(sent, SOCKET_ERROR);
}

std::array<uint8_t, 24> makeHeader(uint32_t sequence, uint32_t payload_size,
                                   uint16_t sample_rate = 48000,
                                   uint16_t channels = 1) {
    std::array<uint8_t, 24> header{};
    writeU32(header.data(), kAudioPacketMagic);
    writeU32(header.data() + 4, sequence);
    writeU64(header.data() + 8, 123456);
    writeU16(header.data() + 16, sample_rate);
    writeU16(header.data() + 18, channels);
    writeU32(header.data() + 20, payload_size);
    return header;
}

class NetworkHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        WSADATA data{};
        ASSERT_EQ(WSAStartup(MAKEWORD(2, 2), &data), 0);
    }

    void TearDown() override {
        WSACleanup();
    }
};

TEST_F(NetworkHandlerTest, RejectsOversizedSendPayload) {
    NetworkHandler network("127.0.0.1", "127.0.0.1", 39201, 39202);
    Packet packet;
    packet.data_size = static_cast<int32_t>(kMaxAudioPacketPayload + 1);
    EXPECT_FALSE(network.send(packet));
}

TEST_F(NetworkHandlerTest, RejectsMalformedWirePacketsAndAcceptsValidPacket) {
    NetworkHandler receiver("127.0.0.1", "127.0.0.1", 39202, 39203);
    SOCKET source = createBoundUdpSocket(39203);

    std::array<uint8_t, 8> truncated{};
    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(static_cast<u_short>(39202));
    InetPtonA(AF_INET, "127.0.0.1", &destination.sin_addr);
    ASSERT_NE(sendto(source, reinterpret_cast<const char*>(truncated.data()),
                     static_cast<int>(truncated.size()), 0,
                     reinterpret_cast<sockaddr*>(&destination), sizeof(destination)), SOCKET_ERROR);
    EXPECT_FALSE(receiver.receive());

    const std::array<uint8_t, 3> payload{10, 20, 30};
    sendRaw(source, 39202, makeHeader(1, 100), payload.data(), payload.size());
    EXPECT_FALSE(receiver.receive());

    sendRaw(source, 39202, makeHeader(2, 3, 44100, 1), payload.data(), payload.size());
    EXPECT_FALSE(receiver.receive());

    sendRaw(source, 39202, makeHeader(3, 3, 48000, 3), payload.data(), payload.size());
    EXPECT_FALSE(receiver.receive());

    sendRaw(source, 39202, makeHeader(4, kMaxAudioPacketPayload + 1), payload.data(), payload.size());
    EXPECT_FALSE(receiver.receive());

    sendRaw(source, 39202, makeHeader(5, 3), payload.data(), payload.size());
    bool received = false;
    for (int attempt = 0; attempt < 20 && !received; ++attempt) {
        received = receiver.receive();
        if (!received) {
            std::this_thread::sleep_for(5ms);
        }
    }
    ASSERT_TRUE(received);

    std::array<uint8_t, 16> output{};
    int32_t output_size = 0;
    ASSERT_TRUE(receiver.getJitterBufferPacket(output.data(), output.size(), output_size));
    EXPECT_EQ(output_size, 3);
    EXPECT_EQ(output[0], 10);
    EXPECT_EQ(output[1], 20);
    EXPECT_EQ(output[2], 30);

    closesocket(source);
}

TEST_F(NetworkHandlerTest, RejectsPacketsFromUnexpectedSource) {
    NetworkHandler receiver("127.0.0.1", "127.0.0.1", 39212, 39213);
    SOCKET unexpected_source = createBoundUdpSocket(39214);
    const std::array<uint8_t, 1> payload{1};
    sendRaw(unexpected_source, 39212, makeHeader(1, 1), payload.data(), payload.size());

    bool received = false;
    for (int attempt = 0; attempt < 10 && !received; ++attempt) {
        received = receiver.receive();
        std::this_thread::sleep_for(5ms);
    }
    EXPECT_FALSE(received);
    closesocket(unexpected_source);
}

} // namespace
