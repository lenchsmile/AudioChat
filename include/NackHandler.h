#pragma once

#include <cstdint>
#include <vector>
#include <deque>
#include <mutex>
#include <memory>
#include <chrono>
#include <functional>
#include <atomic>

/**
 * @struct NackRequest
 * @brief Request to retransmit lost packets
 */
struct NackRequest {
    std::vector<uint32_t> lost_sequence_numbers;
    std::chrono::system_clock::time_point timestamp;
};

/**
 * @class RetransmissionBuffer
 * @brief Store sent packets for potential retransmission
 */
class RetransmissionBuffer {
public:
    /**
     * @brief Constructor
     * @param max_size Maximum number of packets to store
     */
    explicit RetransmissionBuffer(size_t max_size = 100);
    
    /**
     * @brief Add packet to buffer
     * @param sequence_number Packet sequence number
     * @param data Packet data
     * @param size Data size
     */
    void addPacket(uint32_t sequence_number, const uint8_t* data, size_t size);
    
    /**
     * @brief Get packet for retransmission
     * @param sequence_number Packet sequence number
     * @param out_data Output buffer (caller owns)
     * @param out_size Output size
     * @return True if packet found
     */
    bool getPacket(uint32_t sequence_number, std::vector<uint8_t>& out_data);
    
    /**
     * @brief Get multiple packets
     * @param sequence_numbers List of sequence numbers
     * @param out_packets Vector of packets
     * @return Number of packets found
     */
    size_t getPackets(const std::vector<uint32_t>& sequence_numbers,
                     std::vector<std::vector<uint8_t>>& out_packets);
    
    /**
     * @brief Remove packet from buffer (cleanup)
     * @param sequence_number Packet sequence number
     */
    void removePacket(uint32_t sequence_number);
    
    /**
     * @brief Clear all packets older than specified age
     * @param max_age_ms Maximum age in milliseconds
     */
    void clearOlderThan(uint64_t max_age_ms);
    
    /**
     * @brief Get buffer statistics
     * @return Current number of packets in buffer
     */
    size_t getPacketCount() const;
    
    /**
     * @brief Clear all packets
     */
    void clear();

private:
    struct BufferedPacket {
        uint32_t sequence_number;
        std::vector<uint8_t> data;
        std::chrono::system_clock::time_point timestamp;
    };
    
    mutable std::mutex mutex_;
    std::deque<BufferedPacket> packets_;
    size_t max_size_;
};

/**
 * @class NackHandler
 * @brief Handle NACK (Negative Acknowledgment) requests
 * 
 * Features:
 * - Process NACK requests
 * - Trigger retransmission
 * - Manage retransmission buffer
 * - Track retransmission statistics
 */
class NackHandler {
public:
    using RetransmitCallback = std::function<bool(uint32_t seq, const uint8_t* data, size_t size)>;
    
    /**
     * @brief Constructor
     * @param buffer_size Maximum packets to buffer
     */
    explicit NackHandler(size_t buffer_size = 100);
    
    /**
     * @brief Set callback for retransmission
     * @param callback Function to call when retransmitting
     */
    void setRetransmitCallback(RetransmitCallback callback);
    
    /**
     * @brief Add packet to retransmission buffer
     * @param sequence_number Packet sequence number
     * @param data Packet data
     * @param size Data size
     */
    void recordPacketSent(uint32_t sequence_number, const uint8_t* data, size_t size);
    
    /**
     * @brief Process NACK request
     * @param request NACK request
     * @return Number of packets retransmitted
     */
    size_t processNack(const NackRequest& request);
    
    /**
     * @brief Process NACK for single packet
     * @param sequence_number Lost packet sequence number
     * @return True if retransmitted
     */
    bool requestRetransmit(uint32_t sequence_number);
    
    /**
     * @brief Get retransmission statistics
     * @return Number of packets retransmitted
     */
    uint32_t getRetransmittedCount() const;
    
    /**
     * @brief Get NACK statistics
     * @return Number of NACK requests received
     */
    uint32_t getNackCount() const;
    
    /**
     * @brief Reset statistics
     */
    void resetStats();
    
    /**
     * @brief Set maximum packet age
     * @param max_age_ms Maximum age in milliseconds before cleanup
     */
    void setMaxPacketAge(uint64_t max_age_ms);

private:
    std::unique_ptr<RetransmissionBuffer> buffer_;
    RetransmitCallback retransmit_callback_;
    
    std::atomic<uint32_t> retransmitted_count_{0};
    std::atomic<uint32_t> nack_count_{0};
    
    uint64_t max_packet_age_ms_ = 1000;
    
    std::mutex callback_mutex_;
};

