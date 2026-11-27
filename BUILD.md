# Building AudioChat

## Prerequisites

- CMake 3.28 or higher
- Ninja build system
- Clang 16+ or MSVC 2022
- vcpkg package manager (optional, for dependency management)

## Install Dependencies

### Using vcpkg (Recommended)

```bash
# Clone vcpkg if not already installed
git clone https://github.com/microsoft/vcpkg thirdparty/vcpkg

# Install dependencies
thirdparty/vcpkg/vcpkg install portaudio:x64-windows opus:x64-windows
```

### Manual Build

If you prefer to build dependencies manually, place the built libraries in:
- `thirdparty/PortAudio/lib/` - PortAudio library
- `thirdparty/Opus/lib/` - Opus library

## Build

### Using CMake Presets

```bash
# Configure (clang-debug)
cmake --preset clang-debug

# Configure (clang-release)
cmake --preset clang-release

# Build
cmake --build --preset clang-debug

# Or specify preset directly
cmake --build out/build/clang-debug
```

### Manual Build

```bash
# Configure with vcpkg toolchain
cmake -S . -B out/build -G Ninja ^
  -DCMAKE_C_COMPILER=clang ^
  -DCMAKE_CXX_COMPILER=clang++ ^
  -DCMAKE_BUILD_TYPE=Debug ^
  -DCMAKE_TOOLCHAIN_FILE=thirdparty/vcpkg/scripts/buildsystems/vcpkg.cmake

# Build
cmake --build out/build
```

### Using Build Script

```bash
# Build with Debug configuration using Clang
.\build.ps1 -Config Debug -Toolchain Clang

# Build with Release configuration using MSVC
.\build.ps1 -Config Release -Toolchain MSVC
```

## Run

```bash
# From build directory
out/build/clang-debug/bin/AudioChat.exe
```

## Troubleshooting

### PortAudio/Opus not found
Ensure vcpkg is installed and dependencies are installed:
```bash
thirdparty/vcpkg/vcpkg install portaudio:x64-windows opus:x64-windows
```

### Compiler not found
Ensure Clang or MSVC is in your PATH:
```bash
# For Clang
where clang++
# For MSVC
"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
```

### CMake cannot find vcpkg
Set VCPKG_ROOT environment variable:
```bash
$env:VCPKG_ROOT="C:\path\to\vcpkg"
```