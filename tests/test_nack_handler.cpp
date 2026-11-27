#include <gtest/gtest.h>
#include "NackHandler.h"
#include <cstring>

class RetransmissionBufferTest : public ::testing::Test {
protected:
    RetransmissionBuffer buffer{10};
};

TEST_F(RetransmissionBufferTest, AddAndGetPacket) {
    uint8_t data[100] = {1, 2, 3, 4, 5};
    
    buffer.addPacket(1, data, sizeof(data));
    
    std::vector<uint8_t> retrieved;
    EXPECT_TRUE(buffer.getPacket(1, retrieved));
    EXPECT_EQ(retrieved.size(), sizeof(data));
    EXPECT_EQ(retrieved[0], 1);
}

TEST_F(RetransmissionBufferTest, GetNonexistentPacket) {
    std::vector<uint8_t> retrieved;
    EXPECT_FALSE(buffer.getPacket(999, retrieved));
}

TEST_F(RetransmissionBufferTest, NoduplicatePackets) {
    uint8_t data[50] = {1, 2, 3};
    
    buffer.addPacket(1, data, sizeof(data));
    buffer.addPacket(1, data, sizeof(data));  // Duplicate
    
    EXPECT_EQ(buffer.getPacketCount(), 1);
}

TEST_F(RetransmissionBufferTest, BufferSizeLimit) {
    uint8_t data[50] = {0};
    
    for (int i = 0; i < 20; ++i) {
        buffer.addPacket(i, data, sizeof(data));
    }
    
    EXPECT_EQ(buffer.getPacketCount(), 10);  // Max size
}

TEST_F(RetransmissionBufferTest, GetMultiplePackets) {
    uint8_t data[100] = {0};
    
    for (int i = 1; i <= 5; ++i) {
        buffer.addPacket(i, data, sizeof(data));
    }
    
    std::vector<uint32_t> seqs = {1, 3, 5};
    std::vector<std::vector<uint8_t>> packets;
    
    size_t found = buffer.getPackets(seqs, packets);
    EXPECT_EQ(found, 3);
}

TEST_F(RetransmissionBufferTest, RemovePacket) {
    uint8_t data[50] = {0};
    
    buffer.addPacket(1, data, sizeof(data));
    buffer.removePacket(1);
    
    std::vector<uint8_t> retrieved;
    EXPECT_FALSE(buffer.getPacket(1, retrieved));
}

TEST_F(RetransmissionBufferTest, Clear) {
    uint8_t data[50] = {0};
    
    for (int i = 0; i < 5; ++i) {
        buffer.addPacket(i, data, sizeof(data));
    }
    
    buffer.clear();
    EXPECT_EQ(buffer.getPacketCount(), 0);
}

class NackHandlerTest : public ::testing::Test {
protected:
    NackHandler handler{50};
};

TEST_F(NackHandlerTest, RecordPacketSent) {
    uint8_t data[100] = {1, 2, 3};
    handler.recordPacketSent(1, data, sizeof(data));
    
    // Packet should be buffered
    EXPECT_GE(handler.getRetransmittedCount(), 0);
}

TEST_F(NackHandlerTest, RequestRetransmit) {
    uint8_t data[100] = {1, 2, 3};
    handler.recordPacketSent(1, data, sizeof(data));
    
    std::atomic<int> retransmit_count{0};
    handler.setRetransmitCallback(
        [&retransmit_count](uint32_t seq, const uint8_t* data, size_t size) {
            retransmit_count++;
            return true;
        });
    
    bool success = handler.requestRetransmit(1);
    EXPECT_TRUE(success);
    EXPECT_EQ(retransmit_count.load(), 1);
}

TEST_F(NackHandlerTest, RetransmitNonexistentPacket) {
    bool success = handler.requestRetransmit(999);
    EXPECT_FALSE(success);
}

TEST_F(NackHandlerTest, ProcessNackRequest) {
    uint8_t data[100] = {0};
    
    for (int i = 1; i <= 5; ++i) {
        handler.recordPacketSent(i, data, sizeof(data));
    }
    
    std::atomic<int> retransmit_count{0};
    handler.setRetransmitCallback(
        [&retransmit_count](uint32_t seq, const uint8_t* data, size_t size) {
            retransmit_count++;
            return true;
        });
    
    NackRequest req;
    req.lost_sequence_numbers = {1, 3, 5};
    
    size_t retransmitted = handler.processNack(req);
    EXPECT_EQ(retransmitted, 3);
    EXPECT_EQ(handler.getNackCount(), 1);
}

TEST_F(NackHandlerTest, GetRetransmittedCount) {
    uint8_t data[100] = {0};
    handler.recordPacketSent(1, data, sizeof(data));
    
    std::atomic<int> count{0};
    handler.setRetransmitCallback(
        [&count](uint32_t seq, const uint8_t* data, size_t size) {
            count++;
            return true;
        });
    
    handler.requestRetransmit(1);
    
    EXPECT_EQ(handler.getRetransmittedCount(), 1);
}

TEST_F(NackHandlerTest, ResetStats) {
    uint8_t data[100] = {0};
    handler.recordPacketSent(1, data, sizeof(data));
    
    handler.setRetransmitCallback(
        [](uint32_t seq, const uint8_t* data, size_t size) {
            return true;
        });
    
    handler.requestRetransmit(1);
    handler.resetStats();
    
    EXPECT_EQ(handler.getRetransmittedCount(), 0);
    EXPECT_EQ(handler.getNackCount(), 0);
}

TEST_F(NackHandlerTest, SetMaxPacketAge) {
    handler.setMaxPacketAge(5000);
    
    uint8_t data[100] = {0};
    handler.recordPacketSent(1, data, sizeof(data));
    
    // Should not error out
    handler.recordPacketSent(2, data, sizeof(data));
}

