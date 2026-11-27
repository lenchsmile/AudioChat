#include <gtest/gtest.h>
#include "Logger.h"
#include <filesystem>

class LoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        Logger::init("test_logs", "debug");
    }

    void TearDown() override {
        Logger::shutdown();
        // Clean up test logs
        std::filesystem::remove_all("test_logs");
    }
};

TEST_F(LoggerTest, LoggerInitialization) {
    auto logger = Logger::get();
    ASSERT_NE(logger, nullptr);
}

TEST_F(LoggerTest, LoggerLogLevels) {
    auto logger = Logger::get();
    ASSERT_NO_THROW({
        LOG_TRACE("Trace message");
        LOG_DEBUG("Debug message");
        LOG_INFO("Info message");
        LOG_WARN("Warn message");
        LOG_ERROR("Error message");
        LOG_CRITICAL("Critical message");
    });
}

TEST_F(LoggerTest, SetLogLevel) {
    ASSERT_NO_THROW({
        Logger::setLevel("debug");
        Logger::setLevel("info");
        Logger::setLevel("warn");
        Logger::setLevel("error");
    });
}

TEST_F(LoggerTest, LoggerFlush) {
    ASSERT_NO_THROW({
        LOG_INFO("Test message");
        Logger::flush();
    });
}
