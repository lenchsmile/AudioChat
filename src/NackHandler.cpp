#include "NackHandler.h"
#include <algorithm>
#include <functional>

// RetransmissionBuffer implementation
RetransmissionBuffer::RetransmissionBuffer(size_t max_size) : max_size_(max_size) {
}

void RetransmissionBuffer::addPacket(uint32_t sequence_number, const uint8_t* data,
                                     size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Don't add duplicates
    auto it = std::find_if(packets_.begin(), packets_.end(),
        [sequence_number](const BufferedPacket& pkt) {
            return pkt.sequence_number == sequence_number;
        });
    
    if (it != packets_.end()) {
        return;
    }
    
    BufferedPacket pkt;
    pkt.sequence_number = sequence_number;
    pkt.data.assign(data, data + size);
    pkt.timestamp = std::chrono::system_clock::now();
    
    packets_.push_back(pkt);
    
    // Maintain size limit
    while (packets_.size() > max_size_) {
        packets_.pop_front();
    }
}

bool RetransmissionBuffer::getPacket(uint32_t sequence_number,
                                     std::vector<uint8_t>& out_data) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = std::find_if(packets_.begin(), packets_.end(),
        [sequence_number](const BufferedPacket& pkt) {
            return pkt.sequence_number == sequence_number;
        });
    
    if (it == packets_.end()) {
        return false;
    }
    
    out_data = it->data;
    return true;
}

size_t RetransmissionBuffer::getPackets(const std::vector<uint32_t>& sequence_numbers,
                                        std::vector<std::vector<uint8_t>>& out_packets) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t found = 0;
    for (uint32_t seq : sequence_numbers) {
        auto it = std::find_if(packets_.begin(), packets_.end(),
            [seq](const BufferedPacket& pkt) {
                return pkt.sequence_number == seq;
            });
        
        if (it != packets_.end()) {
            out_packets.push_back(it->data);
            found++;
        }
    }
    
    return found;
}

void RetransmissionBuffer::removePacket(uint32_t sequence_number) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = std::find_if(packets_.begin(), packets_.end(),
        [sequence_number](const BufferedPacket& pkt) {
            return pkt.sequence_number == sequence_number;
        });
    
    if (it != packets_.end()) {
        packets_.erase(it);
    }
}

void RetransmissionBuffer::clearOlderThan(uint64_t max_age_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto now = std::chrono::system_clock::now();
    auto threshold = now - std::chrono::milliseconds(max_age_ms);
    
    packets_.erase(
        std::remove_if(packets_.begin(), packets_.end(),
            [threshold](const BufferedPacket& pkt) {
                return pkt.timestamp < threshold;
            }),
        packets_.end()
    );
}

size_t RetransmissionBuffer::getPacketCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return packets_.size();
}

void RetransmissionBuffer::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    packets_.clear();
}

// NackHandler implementation
NackHandler::NackHandler(size_t buffer_size)
    : buffer_(std::make_unique<RetransmissionBuffer>(buffer_size)) {
}

void NackHandler::setRetransmitCallback(RetransmitCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    retransmit_callback_ = callback;
}

void NackHandler::recordPacketSent(uint32_t sequence_number, const uint8_t* data,
                                   size_t size) {
    buffer_->addPacket(sequence_number, data, size);
    
    // Periodic cleanup of old packets
    buffer_->clearOlderThan(max_packet_age_ms_);
}

size_t NackHandler::processNack(const NackRequest& request) {
    nack_count_++;
    
    size_t retransmitted = 0;
    for (uint32_t seq : request.lost_sequence_numbers) {
        if (requestRetransmit(seq)) {
            retransmitted++;
        }
    }
    
    return retransmitted;
}

bool NackHandler::requestRetransmit(uint32_t sequence_number) {
    std::vector<uint8_t> data;
    
    if (!buffer_->getPacket(sequence_number, data)) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (retransmit_callback_) {
        bool success = retransmit_callback_(sequence_number, data.data(), data.size());
        if (success) {
            retransmitted_count_++;
        }
        return success;
    }
    
    return false;
}

uint32_t NackHandler::getRetransmittedCount() const {
    return retransmitted_count_.load();
}

uint32_t NackHandler::getNackCount() const {
    return nack_count_.load();
}

void NackHandler::resetStats() {
    retransmitted_count_ = 0;
    nack_count_ = 0;
}

void NackHandler::setMaxPacketAge(uint64_t max_age_ms) {
    max_packet_age_ms_ = max_age_ms;
}

