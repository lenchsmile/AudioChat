#include "JitterBuffer.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iterator>
#include <utility>

namespace {

bool sequenceIsNewer(uint32_t candidate, uint32_t reference) {
    return candidate != reference &&
           static_cast<int64_t>(static_cast<uint32_t>(candidate - reference)) > 0;
}

} // namespace

JitterBuffer::JitterBuffer(size_t max_packets)
    : max_packets_(std::max<size_t>(1, max_packets)) {
}

JitterBuffer::~JitterBuffer() {
    stop();
}

bool JitterBuffer::addPacket(Packet packet) {
    if (packet.data_size < 0 ||
        static_cast<size_t>(packet.data_size) > packet.payload.size()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (stopped_) {
        return false;
    }
    if (has_last_sequence_ && !sequenceIsNewer(packet.sequence, last_sequence_)) {
        return true;
    }

    const auto duplicate = std::find_if(
        buffer_.begin(), buffer_.end(),
        [&packet](const Packet& queued) { return queued.sequence == packet.sequence; });
    if (duplicate != buffer_.end()) {
        return true;
    }

    if (buffer_.size() >= max_packets_) {
        buffer_.pop_front();
        ++dropped_packets_;
    }
    buffer_.push_back(std::move(packet));
    cv_.notify_one();
    return true;
}

bool JitterBuffer::getPacket(uint8_t* destination, size_t capacity, int32_t& out_size) {
    return getPacketWithTimeout(destination, capacity, out_size, -1) == Result::Packet;
}

JitterBuffer::Result JitterBuffer::getPacketWithTimeout(uint8_t* destination, size_t capacity,
                                                        int32_t& out_size, int timeout_ms) {
    out_size = 0;
    if (!destination) {
        return Result::InvalidDestination;
    }

    std::unique_lock<std::mutex> lock(mutex_);
    if (timeout_ms < 0) {
        cv_.wait(lock, [this] { return stopped_ || !buffer_.empty(); });
    } else {
        cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                     [this] { return stopped_ || !buffer_.empty(); });
    }
    if (stopped_) {
        return Result::Stopped;
    }
    if (buffer_.empty()) {
        return Result::Timeout;
    }

    while (!buffer_.empty()) {
        auto selected = buffer_.begin();
        for (auto candidate = std::next(buffer_.begin()); candidate != buffer_.end(); ++candidate) {
            const bool newer = has_last_sequence_
                ? sequenceIsNewer(candidate->sequence, selected->sequence)
                : candidate->sequence < selected->sequence;
            if (newer) {
                selected = candidate;
            }
        }

        Packet packet = std::move(*selected);
        buffer_.erase(selected);

        if (has_last_sequence_) {
            const uint32_t expected = last_sequence_ + 1;
            if (packet.sequence != expected) {
                ++dropped_packets_;
            }
        }
        last_sequence_ = packet.sequence;
        has_last_sequence_ = true;

        if (static_cast<size_t>(packet.data_size) > capacity) {
            ++dropped_packets_;
            continue;
        }
        if (packet.data_size > 0) {
            std::memcpy(destination, packet.payload.data(),
                        static_cast<size_t>(packet.data_size));
        }
        out_size = packet.data_size;
        return Result::Packet;
    }

    return Result::Timeout;
}

void JitterBuffer::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopped_ = true;
    }
    cv_.notify_all();
}

void JitterBuffer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_.clear();
    last_sequence_ = 0;
    has_last_sequence_ = false;
    dropped_packets_ = 0;
}

size_t JitterBuffer::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return buffer_.size();
}

uint64_t JitterBuffer::droppedPackets() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dropped_packets_;
}
