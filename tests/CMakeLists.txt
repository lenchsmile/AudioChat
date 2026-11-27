# Test executable - only if we have the required dependencies
if(spdlog_FOUND AND Opus_FOUND)
    add_executable(AudioChatTests
        test_logger.cpp
        test_result.cpp
        test_codec.cpp
        test_aec_processor.cpp
        test_reference_signal_buffer.cpp
        test_aec_audio_handler.cpp
        test_codec_optimizer.cpp
        test_phase2_integration.cpp
        test_network_monitor.cpp
        test_heartbeat_manager.cpp
        test_congestion_controller.cpp
        test_nack_handler.cpp
        test_audio_device_monitor.cpp
        test_phase3_integration.cpp
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
        ${CMAKE_SOURCE_DIR}/src/AecProcessor.cpp
        ${CMAKE_SOURCE_DIR}/src/ReferenceSignalBuffer.cpp
        ${CMAKE_SOURCE_DIR}/src/AecAudioHandler.cpp
        ${CMAKE_SOURCE_DIR}/src/CodecOptimizer.cpp
        ${CMAKE_SOURCE_DIR}/src/NetworkMonitor.cpp
        ${CMAKE_SOURCE_DIR}/src/HeartbeatManager.cpp
        ${CMAKE_SOURCE_DIR}/src/CongestionController.cpp
        ${CMAKE_SOURCE_DIR}/src/NackHandler.cpp
        ${CMAKE_SOURCE_DIR}/src/AudioDeviceMonitor.cpp
    )

    # Register tests
    add_test(NAME AudioChatTests COMMAND AudioChatTests)
else()
    message(STATUS "Skipping tests - missing dependencies:")
    if(NOT spdlog_FOUND)
        message(STATUS "  - spdlog not found")
    endif()
    if(NOT Opus_FOUND)
        message(STATUS "  - Opus not found")
    endif()
endif()
