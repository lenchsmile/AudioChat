#include <gtest/gtest.h>
#include "AudioChatEngine.h"

TEST(AudioChatEngineTest, RejectsUnsupportedFrameDurationBeforeOpeningDevices) {
    AudioChatConfig config;
    config.frame_duration_ms = 40;
    AudioChatEngine engine(config);

    const auto result = engine.init();
    ASSERT_TRUE(result.isError());
    EXPECT_FALSE(engine.isRunning());
    EXPECT_NO_THROW(engine.stop());
}

TEST(AudioChatEngineTest, RejectsInvalidPortsBeforeOpeningDevices) {
    AudioChatConfig config;
    config.local_port = 0;
    AudioChatEngine engine(config);

    const auto result = engine.init();
    ASSERT_TRUE(result.isError());
    EXPECT_NO_THROW(engine.stop());
    EXPECT_NO_THROW(engine.stop());
}
