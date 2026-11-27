#include <gtest/gtest.h>
#include "JitterBuffer.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

TEST(JitterBufferTest, RejectsInvalidPayloadSize) {
    JitterBuffer buffer(4);
    Packet packet;
    packet.sequence = 1;
    packet.data_size = static_cast<int32_t>(kMaxAudioPacketPayload + 1);
    EXPECT_FALSE(buffer.addPacket(packet));
    EXPECT_EQ(buffer.size(), 0u);
}

TEST(JitterBufferTest, ReturnsPacketsInSequenceOrder) {
    JitterBuffer buffer(4);
    for (uint32_t sequence : {3u, 1u, 2u}) {
        Packet packet;
        packet.sequence = sequence;
        packet.data_size = 1;
        packet.payload[0] = static_cast<uint8_t>(sequence);
        ASSERT_TRUE(buffer.addPacket(packet));
    }

    std::array<uint8_t, 8> output{};
    int32_t size = 0;
    for (uint32_t expected : {1u, 2u, 3u}) {
        ASSERT_TRUE(buffer.getPacket(output.data(), output.size(), size));
        EXPECT_EQ(size, 1);
        EXPECT_EQ(output[0], expected);
    }
}

TEST(JitterBufferTest, DropsDuplicates) {
    JitterBuffer buffer(4);
    Packet packet;
    packet.sequence = 7;
    packet.data_size = 1;
    packet.payload[0] = 42;
    ASSERT_TRUE(buffer.addPacket(packet));
    ASSERT_TRUE(buffer.addPacket(packet));
    EXPECT_EQ(buffer.size(), 1u);

    std::array<uint8_t, 8> output{};
    int32_t size = 0;
    ASSERT_TRUE(buffer.getPacket(output.data(), output.size(), size));
    EXPECT_EQ(output[0], 42);
}

TEST(JitterBufferTest, CapacityIsBounded) {
    JitterBuffer buffer(2);
    for (uint32_t sequence = 1; sequence <= 10; ++sequence) {
        Packet packet;
        packet.sequence = sequence;
        packet.data_size = 1;
        ASSERT_TRUE(buffer.addPacket(packet));
        EXPECT_LE(buffer.size(), 2u);
    }
    EXPECT_GT(buffer.droppedPackets(), 0u);
}

TEST(JitterBufferTest, InsufficientDestinationDoesNotOverflow) {
    JitterBuffer buffer(4);
    for (uint32_t sequence : {1u, 2u}) {
        Packet packet;
        packet.sequence = sequence;
        packet.data_size = sequence == 1 ? 4 : 2;
        packet.payload.fill(static_cast<uint8_t>(sequence));
        ASSERT_TRUE(buffer.addPacket(packet));
    }

    std::array<uint8_t, 2> small{};
    int32_t size = 0;
    ASSERT_TRUE(buffer.getPacket(small.data(), small.size(), size));
    EXPECT_EQ(size, 2);
    for (const auto byte : small) {
        EXPECT_EQ(byte, 2);
    }
    EXPECT_GT(buffer.droppedPackets(), 0u);
}

TEST(JitterBufferTest, StopWakesBlockedReader) {
    JitterBuffer buffer(4);
    std::array<uint8_t, 8> output{};
    int32_t size = -1;
    std::thread reader([&] {
        EXPECT_FALSE(buffer.getPacket(output.data(), output.size(), size));
    });

    std::this_thread::sleep_for(20ms);
    buffer.stop();
    reader.join();
    EXPECT_EQ(size, 0);
}

TEST(JitterBufferTest, TimeoutDoesNotBlockIndefinitely) {
    JitterBuffer buffer(4);
    std::array<uint8_t, 8> output{};
    int32_t size = -1;
    const auto started = std::chrono::steady_clock::now();
    const auto result = buffer.getPacketWithTimeout(output.data(), output.size(), size, 10);
    const auto elapsed = std::chrono::steady_clock::now() - started;

    EXPECT_EQ(result, JitterBuffer::Result::Timeout);
    EXPECT_EQ(size, 0);
    EXPECT_GE(elapsed, std::chrono::milliseconds(5));
    EXPECT_LT(elapsed, std::chrono::milliseconds(200));
}
