cmake_minimum_required(VERSION 3.28)

project(AudioChat
    VERSION 0.5.0
    DESCRIPTION "Audio Chat Application with Opus codec and PortAudio"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BUILD_TESTING "Build AudioChat tests" ON)
option(BUILD_GUI "Build Qt GUI application" OFF)
option(REQUIRE_AEC "Require a real WebRTC Audio Processing backend" OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

message(STATUS "========================================")
message(STATUS "AudioChat v${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Require AEC: ${REQUIRE_AEC}")
message(STATUS "========================================")

find_package(PortAudio REQUIRED)
find_package(Opus REQUIRED)
find_package(spdlog REQUIRED)
find_package(yaml-cpp REQUIRED)

if(REQUIRE_AEC)
    find_package(WebRtcAudioProcessing REQUIRED)
else()
    find_package(WebRtcAudioProcessing QUIET)
endif()

if(WebRtcAudioProcessing_FOUND)
    message(STATUS "WebRTC Audio Processing found - AEC backend enabled")
    set(HAS_WEBRTC_AEC TRUE)
else()
    message(STATUS "WebRTC Audio Processing not found - developer no-AEC build")
    set(HAS_WEBRTC_AEC FALSE)
endif()

add_library(audiochat_core STATIC
    src/Logger.cpp
    src/Result.cpp
    src/ConfigManager.cpp
    src/AecProcessor.cpp
    src/ReferenceSignalBuffer.cpp
    src/AecAudioHandler.cpp
    src/CodecOptimizer.cpp
    src/AudioHandler.cpp
    src/AudioRecorder.cpp
    src/Codec.cpp
    src/JitterBuffer.cpp
    src/NetworkHandler.cpp
    src/AudioChatEngine.cpp
    src/NetworkMonitor.cpp
    src/HeartbeatManager.cpp
    src/CongestionController.cpp
    src/NackHandler.cpp
    src/AudioDeviceMonitor.cpp
)

target_include_directories(audiochat_core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${PORTAUDIO_INCLUDE_DIRS}
        ${OPUS_INCLUDE_DIRS}
)

target_link_libraries(audiochat_core
    PUBLIC
        PortAudio::PortAudio
        Opus::Opus
        spdlog::spdlog
        yaml-cpp::yaml-cpp
)

if(WIN32)
    target_link_libraries(audiochat_core PUBLIC ws2_32 winmm)
endif()

if(HAS_WEBRTC_AEC)
    target_link_libraries(audiochat_core PUBLIC WebRtcAudioProcessing::WebRtcAudioProcessing)
    target_compile_definitions(audiochat_core PUBLIC HAS_WEBRTC_AEC=1)
endif()

target_compile_definitions(audiochat_core PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(audiochat_core PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
elseif(MSVC)
    target_compile_options(audiochat_core PRIVATE /W4 /permissive-)
endif()

add_executable(AudioChat src/main.cpp)
target_link_libraries(AudioChat PRIVATE audiochat_core)

if(MSVC)
    target_compile_options(AudioChat PRIVATE /W4 /permissive-)
endif()

file(COPY ${CMAKE_SOURCE_DIR}/config/ DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/config)

if(BUILD_TESTING)
    enable_testing()
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        message(STATUS "GTest not found, using FetchContent")
        include(FetchContent)
        FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
        )
        FetchContent_MakeAvailable(googletest)
    endif()
    add_subdirectory(tests)
endif()

install(TARGETS AudioChat
    RUNTIME DESTINATION bin
)
install(FILES $<TARGET_RUNTIME_DLLS:AudioChat>
    DESTINATION bin
)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/config/
    DESTINATION config
)

if(NOT TARGET uninstall)
    configure_file(
        ${CMAKE_SOURCE_DIR}/cmake/uninstall.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
        IMMEDIATE @ONLY
    )
    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
    )
endif()
