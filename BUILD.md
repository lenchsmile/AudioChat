# Building AudioChat

## Requirements

- Windows 10/11 x64
- Visual Studio 2026 with C++ support
- CMake 3.28 or newer
- Ninja (only for Ninja presets)
- vcpkg

The repository uses manifest mode. Do not install only PortAudio and Opus: `spdlog` and `yaml-cpp` are also required, and tests require GoogleTest.

## 1. Prepare vcpkg

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\dev\vcpkg
C:\dev\vcpkg\bootstrap-vcpkg.bat -disableMetrics
$env:VCPKG_ROOT = "C:\dev\vcpkg"
```

The manifest pins the vcpkg baseline and dependency versions.

## 2. Configure

```powershell
cmake -S . -B out/build/msvc-phase0 -G "Visual Studio 18 2026" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DBUILD_TESTING=ON `
  -DREQUIRE_AEC=OFF
```

Important options:

| Option | Default | Meaning |
|---|---:|---|
| `BUILD_TESTING` | `ON` | Build and register GoogleTest tests |
| `BUILD_GUI` | `OFF` | Qt GUI is not part of the current release scope |
| `REQUIRE_AEC` | `OFF` | Fail configuration if WebRTC APM is unavailable |

`REQUIRE_AEC=ON` is reserved for the production AEC build after stage 2 is complete.

## 3. Build and test

```powershell
cmake --build out/build/msvc-phase0 --config Debug --parallel
ctest --test-dir out/build/msvc-phase0 -C Debug --output-on-failure --timeout 20

cmake --build out/build/msvc-phase0 --config Release --parallel
ctest --test-dir out/build/msvc-phase0 -C Release --output-on-failure --timeout 20
```

The test target uses GoogleTest discovery and per-test timeouts. A test that deadlocks is reported as a failure instead of hanging indefinitely.

## 4. Run smoke checks

```powershell
.\out\build\msvc-phase0\bin\Release\AudioChat.exe --version
.\out\build\msvc-phase0\bin\Release\AudioChat.exe --check
.\out\build\msvc-phase0\bin\Release\AudioChat.exe --list-devices
.\out\build\msvc-phase0\bin\Release\AudioChat.exe --duration 2
```

`--duration 2` opens the default audio devices, starts the engine, and verifies graceful shutdown.

## 5. Install

```powershell
cmake --install out/build/msvc-phase0 --config Release --prefix .\dist\audiochat
```

The install tree contains:

```text
bin/
  AudioChat.exe
  portaudio.dll
  opus.dll
  spdlog.dll
  fmt.dll
  yaml-cpp.dll
config/
  default.yaml
  presets/
    low-latency.yaml
    balanced.yaml
    high-quality.yaml
```

## Troubleshooting

### Dependencies are not found

Confirm that `CMAKE_TOOLCHAIN_FILE` points to your vcpkg checkout and that the triplet is `x64-windows`. CMake now treats PortAudio, Opus, spdlog and yaml-cpp as required; it will not silently skip the executable.

### No audio device

Run `--list-devices` and set `audio.input_device` / `audio.output_device` to a listed name, or use `auto`.

### Build preset cannot find `cl.exe`

The Ninja presets require a Developer Command Prompt. The Visual Studio generator shown above does not.
