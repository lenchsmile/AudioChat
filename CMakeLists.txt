cmake_minimum_required(VERSION 3.28)

# Project information
project(AudioChat
    VERSION 1.0.0
    DESCRIPTION "Audio Chat Application with Opus codec and PortAudio"
    LANGUAGES CXX
)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set default build type to Release if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# CMake modules path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Print configuration info
message(STATUS "========================================")
message(STATUS "Configuration Summary:")
message(STATUS "  Project: ${PROJECT_NAME} v${PROJECT_VERSION}")
message(STATUS "  Generator: ${CMAKE_GENERATOR}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Source dir: ${CMAKE_SOURCE_DIR}")
message(STATUS "  Build dir: ${CMAKE_BINARY_DIR}")
message(STATUS "  C++ compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "========================================")

# Generate compile_commands.json for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find dependencies
find_package(PortAudio REQUIRED)
find_package(Opus REQUIRED)

# Create executable
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/AudioHandler.cpp
    src/AudioRecorder.cpp
    src/Codec.cpp
    src/JitterBuffer.cpp
    src/NetworkHandler.cpp
)

# Target properties
set_target_properties(${PROJECT_NAME} PROPERTIES
    VERSION ${PROJECT_VERSION}
    OUTPUT_NAME ${PROJECT_NAME}
    CXX_STANDARD ${CMAKE_CXX_STANDARD}
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# Target include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${PORTAUDIO_INCLUDE_DIRS}
    ${OPUS_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    PortAudio::PortAudio
    Opus::Opus
    ws2_32
    winmm
)

# Compile definitions based on build type
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
)

# Compiler warnings for Clang/GCC
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
    )
endif()

# Copy config file to binary directory
configure_file(${CMAKE_SOURCE_DIR}/config.ini
    ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/config.ini
    COPYONLY
)

# Uninstall target
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
