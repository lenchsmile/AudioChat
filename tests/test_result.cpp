#include <gtest/gtest.h>
#include "Result.h"
#include "Logger.h"
#include <memory>

class ResultTest : public ::testing::Test {
protected:
    void SetUp() override {
        Logger::init("test_logs", "error");  // Suppress logs during tests
    }

    void TearDown() override {
        Logger::shutdown();
    }
};

TEST_F(ResultTest, SuccessResult) {
    Result<int> result = Result<int>::ok(42);
    EXPECT_TRUE(result.isOk());
    EXPECT_FALSE(result.isError());
    EXPECT_EQ(result.value(), 42);
    EXPECT_EQ(result.error(), AudioChatError::Success);
}

TEST_F(ResultTest, ErrorResult) {
    Result<int> result = Result<int>::error(AudioChatError::InvalidArgument, "Invalid value");
    EXPECT_FALSE(result.isOk());
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.error(), AudioChatError::InvalidArgument);
    EXPECT_EQ(result.message(), "Invalid value");
}

TEST_F(ResultTest, VoidResultOk) {
    Result<void> result = Result<void>::ok();
    EXPECT_TRUE(result.isOk());
    EXPECT_FALSE(result.isError());
}

TEST_F(ResultTest, VoidResultError) {
    Result<void> result = Result<void>::error(
        AudioChatError::AudioDeviceNotFound, 
        "Device not found"
    );
    EXPECT_FALSE(result.isOk());
    EXPECT_TRUE(result.isError());
    EXPECT_EQ(result.error(), AudioChatError::AudioDeviceNotFound);
}

TEST_F(ResultTest, ErrorDescription) {
    std::string desc = getErrorDescription(AudioChatError::AudioDeviceNotFound);
    EXPECT_EQ(desc, "Audio device not found");
}

TEST_F(ResultTest, ResultToString) {
    Result<int> result = Result<int>::error(
        AudioChatError::CodecInitFailed, 
        "Codec init failed"
    );
    std::string str = result.toString();
    EXPECT_TRUE(str.find("CodecInit") != std::string::npos || 
                str.find("Codec init failed") != std::string::npos);
}

TEST_F(ResultTest, ThrowIfError) {
    Result<void> error_result = Result<void>::error(
        AudioChatError::InternalError, 
        "Test error"
    );
    EXPECT_THROW({
        error_result.throwIfError();
    }, std::runtime_error);

    Result<void> ok_result = Result<void>::ok();
    EXPECT_NO_THROW({
        ok_result.throwIfError();
    });
}

TEST_F(ResultTest, SupportsMoveOnlyValues) {
    auto value = std::make_unique<int>(42);
    auto result = Result<std::unique_ptr<int>>::ok(std::move(value));

    ASSERT_TRUE(result.isOk());
    ASSERT_NE(result.value(), nullptr);
    EXPECT_EQ(*result.value(), 42);
    EXPECT_EQ(value, nullptr);
}

TEST_F(ResultTest, MoveOnlyErrorHasNoValue) {
    auto result = Result<std::unique_ptr<int>>::error(
        AudioChatError::InternalError, "No value"
    );

    ASSERT_TRUE(result.isError());
    EXPECT_THROW(result.value(), std::bad_optional_access);
}
