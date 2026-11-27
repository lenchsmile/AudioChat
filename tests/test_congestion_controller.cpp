#include <gtest/gtest.h>
#include "CongestionController.h"

class CongestionControllerTest : public ::testing::Test {
protected:
    CongestionController controller;
};

TEST_F(CongestionControllerTest, Initialization) {
    EXPECT_EQ(controller.getCongestionLevel(), CongestionLevel::None);
}

TEST_F(CongestionControllerTest, NoCongestionWithLowLoss) {
    controller.updatePacketLossRate(1.0);
    EXPECT_EQ(controller.getCongestionLevel(), CongestionLevel::None);
}

TEST_F(CongestionControllerTest, MildCongestionDetection) {
    for (int i = 0; i < 20; ++i) {
        controller.updatePacketLossRate(5.5);
    }
    
    EXPECT_EQ(controller.getCongestionLevel(), CongestionLevel::Mild);
}

TEST_F(CongestionControllerTest, ModerateCongestionDetection) {
    for (int i = 0; i < 20; ++i) {
        controller.updatePacketLossRate(10.0);
    }
    
    EXPECT_EQ(controller.getCongestionLevel(), CongestionLevel::Moderate);
}

TEST_F(CongestionControllerTest, SevereCongestionDetection) {
    for (int i = 0; i < 20; ++i) {
        controller.updatePacketLossRate(20.0);
    }
    
    EXPECT_EQ(controller.getCongestionLevel(), CongestionLevel::Severe);
}

TEST_F(CongestionControllerTest, RttBasedCongestion) {
    for (int i = 0; i < 20; ++i) {
        controller.updateRtt(250.0);  // Above threshold
    }
    
    EXPECT_EQ(controller.getCongestionLevel(), CongestionLevel::Mild);
}

TEST_F(CongestionControllerTest, BitrateReductionUnderCongestion) {
    uint32_t current_bitrate = 32;
    
    for (int i = 0; i < 20; ++i) {
        controller.updatePacketLossRate(5.5);
    }
    
    auto rec = controller.getRecommendation(current_bitrate);
    EXPECT_TRUE(rec.should_reduce);
    EXPECT_LT(rec.recommended_bitrate_kbps, current_bitrate);
}

TEST_F(CongestionControllerTest, BitrateIncreaseWithGoodConditions) {
    uint32_t current_bitrate = 32;
    
    for (int i = 0; i < 20; ++i) {
        controller.updatePacketLossRate(0.5);
        controller.updateRtt(30.0);
    }
    
    auto rec = controller.getRecommendation(current_bitrate);
    EXPECT_TRUE(rec.should_increase);
}

TEST_F(CongestionControllerTest, SetBitrateLimits) {
    controller.setBitrateLimits(8, 128);
    
    for (int i = 0; i < 20; ++i) {
        controller.updatePacketLossRate(0.1);
    }
    
    auto rec = controller.getRecommendation(200);  // Above max
    EXPECT_LE(rec.recommended_bitrate_kbps, 128);
}

TEST_F(CongestionControllerTest, SendRateLimitNoCongestion) {
    uint32_t limit = controller.getSendRateLimit();
    EXPECT_EQ(limit, 1000);  // No limit
}

TEST_F(CongestionControllerTest, SendRateLimitMildCongestion) {
    for (int i = 0; i < 20; ++i) {
        controller.updatePacketLossRate(5.5);
    }
    
    uint32_t limit = controller.getSendRateLimit();
    EXPECT_EQ(limit, 500);  // 50% reduction
}

TEST_F(CongestionControllerTest, SendRateLimitSevereCongestion) {
    for (int i = 0; i < 20; ++i) {
        controller.updatePacketLossRate(20.0);
    }
    
    uint32_t limit = controller.getSendRateLimit();
    EXPECT_EQ(limit, 100);  // 90% reduction
}

TEST_F(CongestionControllerTest, Reset) {
    for (int i = 0; i < 20; ++i) {
        controller.updatePacketLossRate(10.0);
    }
    
    EXPECT_EQ(controller.getCongestionLevel(), CongestionLevel::Moderate);
    
    controller.reset();
    
    EXPECT_EQ(controller.getCongestionLevel(), CongestionLevel::None);
}

TEST_F(CongestionControllerTest, ThresholdConfiguration) {
    controller.setThresholds(10.0, 300.0);  // Higher thresholds
    
    // Previously mild should now be none
    for (int i = 0; i < 20; ++i) {
        controller.updatePacketLossRate(5.5);
    }
    
    EXPECT_EQ(controller.getCongestionLevel(), CongestionLevel::None);
}

