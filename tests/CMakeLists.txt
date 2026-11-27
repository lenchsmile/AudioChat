# Test executable
add_executable(AudioChatTests
    test_logger.cpp
    test_result.cpp
    test_codec.cpp
)

# Link libraries
target_link_libraries(AudioChatTests PRIVATE
    GTest::gtest
    GTest::gtest_main
    spdlog::spdlog
    Opus::Opus
)

# Include paths
target_include_directories(AudioChatTests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Link main library code (excluding main.cpp)
target_sources(AudioChatTests PRIVATE
    ${CMAKE_SOURCE_DIR}/src/Logger.cpp
    ${CMAKE_SOURCE_DIR}/src/Result.cpp
    ${CMAKE_SOURCE_DIR}/src/Codec.cpp
)

# Register tests
add_test(NAME AudioChatTests COMMAND AudioChatTests)
