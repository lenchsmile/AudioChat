# AudioChat

Audio Chat Application with Opus codec and PortAudio.

## Features

- Real-time audio chat over network
- Opus codec for audio compression
- PortAudio for audio I/O
- Jitter buffer for network packet reordering
- Audio recording capability

## Project Structure

```
AudioChat/
├── CMakeLists.txt             # Main CMake configuration
├── CMakePresets.json         # Build presets for different configurations
├── README.md                 # This file
├── BUILD.md                  # Build instructions
├── .clang-format             # Code formatting configuration
├── .clang-tidy               # Code analysis configuration
├── .gitignore                # Git ignore rules
├── config.ini                # Application configuration
├── cmake/                    # Custom CMake modules
│   ├── FindPortAudio.cmake
│   ├── FindOpus.cmake
│   └── uninstall.cmake.in
├── include/                  # Header files
│   ├── AudioHandler.h
│   ├── AudioRecorder.h
│   ├── Codec.h
│   ├── JitterBuffer.h
│   ├── NetworkHandler.h
│   └── SpscFixedSlots.h
├── src/                      # Source files
│   ├── main.cpp
│   ├── AudioHandler.cpp
│   ├── AudioRecorder.cpp
│   ├── Codec.cpp
│   ├── JitterBuffer.cpp
│   └── NetworkHandler.cpp
├── thirdparty/               # Third-party dependencies
│   ├── Opus/
│   └── PortAudio/
└── out/                      # Build output (generated)
    └── build/
        ├── clang-debug/
        ├── clang-release/
        ├── msvc-debug/
        └── msvc-release/
```

## Requirements

- CMake 3.28 or higher
- Ninja build system
- Clang 16+ or MSVC 2022
- PortAudio library
- Opus library

## Build

### Using CMake Presets (Recommended)

```bash
# Configure with Clang Debug
cmake --preset clang-debug

# Configure with Clang Release
cmake --preset clang-release

# Build
cmake --build --preset clang-debug

# Or build directly
cmake --build out/build/clang-debug

# Run tests
ctest --preset clang-debug

# Install
cmake --install out/build/clang-debug

# Uninstall
cmake --build out/build/clang-debug --target uninstall
```

### Using Build Script

```bash
# Build with Debug configuration using Clang
.\build.ps1 -Config Debug -Toolchain Clang

# Build with Release configuration using MSVC
.\build.ps1 -Config Release -Toolchain MSVC
```

### Manual Configuration

```bash
# Configure
cmake -S . -B out/build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++

# Build
cmake --build out/build
```

## CMake Presets

### clang-debug
Debug build with Clang compiler
- Binary dir: `out/build/clang-debug`
- Generator: Ninja
- Compiler: Clang

### clang-release
Release build with Clang compiler
- Binary dir: `out/build/clang-release`
- Generator: Ninja
- Compiler: Clang

### msvc-debug
Debug build with MSVC compiler
- Binary dir: `out/build/msvc-debug`
- Generator: Ninja
- Compiler: MSVC

### msvc-release
Release build with MSVC compiler
- Binary dir: `out/build/msvc-release`
- Generator: Ninja
- Compiler: MSVC

## Dependencies

Dependencies are located in the `thirdparty/` directory:
- **PortAudio**: Cross-platform audio I/O library
- **Opus**: Audio codec for speech and music

For Windows, the project is configured to use vcpkg for dependency management.

## License

MIT License