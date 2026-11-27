#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <condition_variable>

constexpr uint32_t kAudioPacketMagic = 0x41554832; // "AUH2"
constexpr size_t kAudioPacketHeaderSize = 28;
constexpr size_t kMaxAudioPacketPayload = 1500;

struct Packet {
    uint32_t sequence = 0;
    uint64_t timestamp_us = 0;
    uint16_t sample_rate = 48000;
    uint16_t channels = 1;
    uint16_t frame_duration_ms = 20;
    int32_t data_size = 0;
    std::array<uint8_t, kMaxAudioPacketPayload> payload{};
};

class JitterBuffer {
public:
    enum class Result {
        Packet,
        Timeout,
        Stopped,
        InvalidDestination
    };

    explicit JitterBuffer(size_t max_packets);
    ~JitterBuffer();

    bool addPacket(Packet packet);
    bool getPacket(uint8_t* destination, size_t capacity, int32_t& out_size);
    Result getPacketWithTimeout(uint8_t* destination, size_t capacity,
                                int32_t& out_size, int timeout_ms);
    void stop();
    void clear();

    size_t size() const;
    uint64_t droppedPackets() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Packet> buffer_;
    size_t max_packets_;
    uint32_t last_sequence_ = 0;
    bool has_last_sequence_ = false;
    bool stopped_ = false;
    uint64_t dropped_packets_ = 0;
};
