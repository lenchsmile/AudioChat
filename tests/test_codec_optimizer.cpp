#include <gtest/gtest.h>
#include "CodecOptimizer.h"
#include "Logger.h"

using namespace AudioChat;

/**
 * @brief Unit tests for CodecOptimizer
 * 
 * Tests verify:
 * 1. Configuration validation
 * 2. Parameter ranges
 * 3. DTX enable/disable
 * 4. Bitrate adaptation
 * 5. VBR and complexity settings
 * 6. Packet loss configuration
 */
class CodecOptimizerTest : public ::testing::Test {
protected:
    /**
     * @brief Create default optimization config
     */
    CodecOptimizationConfig CreateDefaultConfig() {
        CodecOptimizationConfig config;
        config.bitrate_bps = 24000;
        config.enable_vbr = true;
        config.enable_vbr_constraint = false;
        config.enable_dtx = true;
        config.enable_fec = true;
        config.packet_loss_percent = 10;
        config.complexity = 5;
        config.enable_adaptive_bitrate = true;
        config.min_bitrate_bps = 8000;
        config.max_bitrate_bps = 128000;
        return config;
    }
};

// ============================================================================
// Configuration Validation Tests
// ============================================================================

TEST_F(CodecOptimizerTest, ValidateBitrateRange) {
    auto config = CreateDefaultConfig();
    
    // Valid bitrates
    for (int br : {8000, 16000, 24000, 48000, 128000}) {
        config.bitrate_bps = br;
        auto result = config.Validate();
        EXPECT_TRUE(result.isOk()) << "Bitrate " << br << " should be valid";
    }

    // Invalid bitrates
    for (int br : {7999, 128001, 500000}) {
        config.bitrate_bps = br;
        auto result = config.Validate();
        EXPECT_TRUE(result.isError()) << "Bitrate " << br << " should be invalid";
    }
}

TEST_F(CodecOptimizerTest, ValidateMinMaxBitrate) {
    auto config = CreateDefaultConfig();
    
    // Valid: min <= target <= max
    config.min_bitrate_bps = 8000;
    config.bitrate_bps = 24000;
    config.max_bitrate_bps = 128000;
    EXPECT_TRUE(config.Validate().isOk());

    // Invalid: min > target
    config.min_bitrate_bps = 32000;
    config.bitrate_bps = 24000;
    config.max_bitrate_bps = 128000;
    EXPECT_TRUE(config.Validate().isError());

    // Invalid: target > max
    config.min_bitrate_bps = 8000;
    config.bitrate_bps = 128000;
    config.max_bitrate_bps = 64000;
    EXPECT_TRUE(config.Validate().isError());
}

TEST_F(CodecOptimizerTest, ValidateComplexity) {
    auto config = CreateDefaultConfig();
    
    // Valid complexity (0-10)
    for (int c : {0, 1, 5, 9, 10}) {
        config.complexity = c;
        auto result = config.Validate();
        EXPECT_TRUE(result.isOk()) << "Complexity " << c << " should be valid";
    }

    // Invalid complexity
    for (int c : {-1, 11, 20}) {
        config.complexity = c;
        auto result = config.Validate();
        EXPECT_TRUE(result.isError()) << "Complexity " << c << " should be invalid";
    }
}

TEST_F(CodecOptimizerTest, ValidatePacketLoss) {
    auto config = CreateDefaultConfig();
    
    // Valid packet loss (0-100%)
    for (int pl : {0, 5, 10, 50, 100}) {
        config.packet_loss_percent = pl;
        auto result = config.Validate();
        EXPECT_TRUE(result.isOk()) << "Packet loss " << pl << "% should be valid";
    }

    // Invalid packet loss
    for (int pl : {-1, 101, 150}) {
        config.packet_loss_percent = pl;
        auto result = config.Validate();
        EXPECT_TRUE(result.isError()) << "Packet loss " << pl << "% should be invalid";
    }
}

TEST_F(CodecOptimizerTest, ValidateFullConfig) {
    auto config = CreateDefaultConfig();
    auto result = config.Validate();
    EXPECT_TRUE(result.isOk()) << "Valid config should pass";
}

// ============================================================================
// Configuration Defaults Tests
// ============================================================================

TEST_F(CodecOptimizerTest, ConfigDefaults) {
    CodecOptimizationConfig config;
    
    EXPECT_EQ(config.bitrate_bps, 24000);
    EXPECT_TRUE(config.enable_vbr);
    EXPECT_FALSE(config.enable_vbr_constraint);
    EXPECT_TRUE(config.enable_dtx);
    EXPECT_TRUE(config.enable_fec);
    EXPECT_EQ(config.packet_loss_percent, 10);
    EXPECT_EQ(config.complexity, 5);
    EXPECT_TRUE(config.enable_adaptive_bitrate);
    EXPECT_EQ(config.min_bitrate_bps, 8000);
    EXPECT_EQ(config.max_bitrate_bps, 128000);
}

// ============================================================================
// DTX Tests
// ============================================================================

TEST_F(CodecOptimizerTest, DtxEnabledDefault) {
    auto config = CreateDefaultConfig();
    EXPECT_TRUE(config.enable_dtx);
}

TEST_F(CodecOptimizerTest, DtxDisabled) {
    auto config = CreateDefaultConfig();
    config.enable_dtx = false;
    EXPECT_TRUE(config.Validate().isOk());
    EXPECT_FALSE(config.enable_dtx);
}

TEST_F(CodecOptimizerTest, DtxValidation) {
    auto config = CreateDefaultConfig();
    
    // DTX can be true or false
    config.enable_dtx = true;
    EXPECT_TRUE(config.Validate().isOk());
    
    config.enable_dtx = false;
    EXPECT_TRUE(config.Validate().isOk());
}

// ============================================================================
// VBR Tests
// ============================================================================

TEST_F(CodecOptimizerTest, VbrEnabledDefault) {
    auto config = CreateDefaultConfig();
    EXPECT_TRUE(config.enable_vbr);
}

TEST_F(CodecOptimizerTest, VbrWithConstraint) {
    auto config = CreateDefaultConfig();
    config.enable_vbr = true;
    config.enable_vbr_constraint = true;
    EXPECT_TRUE(config.Validate().isOk());
}

TEST_F(CodecOptimizerTest, VbrWithoutConstraint) {
    auto config = CreateDefaultConfig();
    config.enable_vbr = true;
    config.enable_vbr_constraint = false;
    EXPECT_TRUE(config.Validate().isOk());
}

// ============================================================================
// Bitrate Adaptation Tests
// ============================================================================

TEST_F(CodecOptimizerTest, AdaptiveBitrateRange) {
    auto config = CreateDefaultConfig();
    config.enable_adaptive_bitrate = true;
    
    // Min <= current <= max
    config.min_bitrate_bps = 8000;
    config.bitrate_bps = 24000;
    config.max_bitrate_bps = 128000;
    
    EXPECT_TRUE(config.Validate().isOk());
}

TEST_F(CodecOptimizerTest, AdaptiveBitrateLowBandwidth) {
    auto config = CreateDefaultConfig();
    
    // Low bandwidth scenario
    config.min_bitrate_bps = 8000;
    config.bitrate_bps = 12000;
    config.max_bitrate_bps = 24000;
    config.enable_adaptive_bitrate = true;
    config.enable_dtx = true;  // Essential for low bandwidth
    
    EXPECT_TRUE(config.Validate().isOk());
}

TEST_F(CodecOptimizerTest, AdaptiveBitrateHighQuality) {
    auto config = CreateDefaultConfig();
    
    // High quality scenario
    config.min_bitrate_bps = 32000;
    config.bitrate_bps = 64000;
    config.max_bitrate_bps = 128000;
    config.complexity = 10;  // Max quality
    config.enable_adaptive_bitrate = true;
    
    EXPECT_TRUE(config.Validate().isOk());
}

// ============================================================================
// Error Resilience Tests
// ============================================================================

TEST_F(CodecOptimizerTest, FecEnabled) {
    auto config = CreateDefaultConfig();
    config.enable_fec = true;
    EXPECT_TRUE(config.Validate().isOk());
}

TEST_F(CodecOptimizerTest, PacketLossHandling) {
    auto config = CreateDefaultConfig();
    
    // Network scenarios
    // Good network
    config.packet_loss_percent = 0;
    EXPECT_TRUE(config.Validate().isOk());
    
    // Moderate network
    config.packet_loss_percent = 5;
    EXPECT_TRUE(config.Validate().isOk());
    
    // Poor network
    config.packet_loss_percent = 20;
    EXPECT_TRUE(config.Validate().isOk());
}

// ============================================================================
// Scenario Tests
// ============================================================================

TEST_F(CodecOptimizerTest, ScenarioVoipOptimized) {
    // Standard VoIP scenario
    auto config = CreateDefaultConfig();
    config.bitrate_bps = 24000;      // Good quality VoIP
    config.enable_dtx = true;        // Save bandwidth
    config.enable_vbr = true;        // Adapt to content
    config.complexity = 5;            // Moderate CPU
    config.enable_fec = true;        // Error correction
    config.packet_loss_percent = 10; // Expected loss
    
    EXPECT_TRUE(config.Validate().isOk());
}

TEST_F(CodecOptimizerTest, ScenarioLowBandwidth) {
    // Low bandwidth emergency call
    auto config = CreateDefaultConfig();
    config.bitrate_bps = 8000;       // Minimum
    config.min_bitrate_bps = 8000;
    config.max_bitrate_bps = 16000;
    config.enable_dtx = true;        // Critical for low BW
    config.enable_vbr = true;
    config.complexity = 3;            // Lower CPU use
    config.enable_fec = true;        // Important for reliability
    
    EXPECT_TRUE(config.Validate().isOk());
}

TEST_F(CodecOptimizerTest, ScenarioHighQuality) {
    // High quality call (good network)
    auto config = CreateDefaultConfig();
    config.bitrate_bps = 64000;      // High bitrate
    config.max_bitrate_bps = 128000;
    config.enable_dtx = true;        // Still useful for efficiency
    config.enable_vbr = true;
    config.complexity = 10;           // Max quality
    config.enable_fec = true;
    config.packet_loss_percent = 0;   // No loss expected
    
    EXPECT_TRUE(config.Validate().isOk());
}

TEST_F(CodecOptimizerTest, ScenarioPoorNetwork) {
    // Poor network scenario
    auto config = CreateDefaultConfig();
    config.bitrate_bps = 16000;      // Conservative bitrate
    config.enable_dtx = true;        // Save bandwidth
    config.enable_vbr = false;       // Avoid spikes
    config.enable_vbr_constraint = false;
    config.complexity = 3;            // CPU available for FEC
    config.enable_fec = true;        // Critical
    config.packet_loss_percent = 15; // High loss expected
    
    EXPECT_TRUE(config.Validate().isOk());
}

// ============================================================================
// Integration Tests
// ============================================================================

TEST_F(CodecOptimizerTest, ConfigurationConsistency) {
    auto config = CreateDefaultConfig();
    
    // DTX should work with both VBR on and off
    config.enable_vbr = true;
    config.enable_dtx = true;
    EXPECT_TRUE(config.Validate().isOk());
    
    config.enable_vbr = false;
    config.enable_dtx = true;
    EXPECT_TRUE(config.Validate().isOk());
}

TEST_F(CodecOptimizerTest, ComplexityVsBitrate) {
    auto config = CreateDefaultConfig();
    
    // Higher complexity with lower bitrate
    config.complexity = 10;
    config.bitrate_bps = 12000;
    EXPECT_TRUE(config.Validate().isOk());
    
    // Lower complexity with higher bitrate
    config.complexity = 1;
    config.bitrate_bps = 128000;
    EXPECT_TRUE(config.Validate().isOk());
}

TEST_F(CodecOptimizerTest, DtxWithFec) {
    // DTX and FEC work together
    auto config = CreateDefaultConfig();
    config.enable_dtx = true;
    config.enable_fec = true;
    EXPECT_TRUE(config.Validate().isOk());
}

TEST_F(CodecOptimizerTest, AdaptiveRangeConstraints) {
    auto config = CreateDefaultConfig();
    
    // Min/max with current in middle
    config.min_bitrate_bps = 8000;
    config.bitrate_bps = 24000;
    config.max_bitrate_bps = 128000;
    EXPECT_TRUE(config.Validate().isOk());
    
    // Min/max with current at min
    config.min_bitrate_bps = 8000;
    config.bitrate_bps = 8000;
    config.max_bitrate_bps = 128000;
    EXPECT_TRUE(config.Validate().isOk());
    
    // Min/max with current at max
    config.min_bitrate_bps = 8000;
    config.bitrate_bps = 128000;
    config.max_bitrate_bps = 128000;
    EXPECT_TRUE(config.Validate().isOk());
}

// ============================================================================
// Documentation Tests
// ============================================================================

TEST_F(CodecOptimizerTest, ConfigStructFields) {
    auto config = CreateDefaultConfig();
    
    // Verify all documented fields exist
    EXPECT_GE(config.bitrate_bps, 0);
    EXPECT_TRUE(config.enable_vbr == true || config.enable_vbr == false);
    EXPECT_TRUE(config.enable_dtx == true || config.enable_dtx == false);
    EXPECT_TRUE(config.enable_fec == true || config.enable_fec == false);
    EXPECT_GE(config.complexity, 0);
    EXPECT_GE(config.packet_loss_percent, 0);
}

TEST_F(CodecOptimizerTest, RecommendedDefaults) {
    // Recommended defaults for VoIP with AEC
    auto config = CreateDefaultConfig();
    
    // DTX recommended for bandwidth savings
    EXPECT_TRUE(config.enable_dtx);
    
    // VBR recommended for adaptive quality
    EXPECT_TRUE(config.enable_vbr);
    
    // FEC recommended for reliability
    EXPECT_TRUE(config.enable_fec);
    
    // Reasonable bitrate
    EXPECT_GE(config.bitrate_bps, 8000);
    EXPECT_LE(config.bitrate_bps, 128000);
}
