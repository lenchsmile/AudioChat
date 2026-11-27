add_executable(AudioChatTests
    test_logger.cpp
    test_result.cpp
    test_codec.cpp
    test_jitter_buffer.cpp
    test_network_handler.cpp
    test_audiochat_engine.cpp
    test_config_manager.cpp
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

target_link_libraries(AudioChatTests PRIVATE
    audiochat_core
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(AudioChatTests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

if(MSVC)
    target_compile_options(AudioChatTests PRIVATE /W4 /permissive-)
endif()

include(GoogleTest)
gtest_discover_tests(AudioChatTests
    DISCOVERY_TIMEOUT 30
    PROPERTIES TIMEOUT 30
)
