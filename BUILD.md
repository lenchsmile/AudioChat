# Building AudioChat

## Requirements

- Windows 10/11 x64
- Visual Studio 2026 with C++ support
- CMake 4.2 or newer
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
| `BUILD_ACOUSTIC_TOOL` | `OFF` | Build the optional real-device AEC validation tool |
| `REQUIRE_AEC` | `OFF` | Fail configuration if WebRTC APM is unavailable |

`REQUIRE_AEC=ON` is required for production and AEC acceptance builds. The `msvc-aec-release` preset enables it together with the vcpkg `aec` feature.

### WebRTC AEC build

The AEC backend is an opt-in vcpkg manifest feature because the full WebRTC static library has a large build cost.

```powershell
$env:VCPKG_ROOT = "C:\path\to\vcpkg"
cmake --preset msvc-aec-release
cmake --build --preset msvc-aec-release --config Release
ctest --preset msvc-aec-release -C Release --output-on-failure
```

The preset sets both `VCPKG_MANIFEST_FEATURES=aec` and `REQUIRE_AEC=ON`. Configuration fails if the WebRTC Audio Processing backend is unavailable. The resulting Release suite contains 250 tests, including synthetic ERLE, double-talk, 20 ms processing, strict-underflow and explicit-passthrough coverage.

Protocol v2 (`AUH2`) includes sample rate, channel count and frame duration. Both peers must use the same audio format and 10/20 ms frame duration; incompatible packets are rejected. AUH2 is not wire-compatible with AUH1.

For manual real-device validation, configure with `-DBUILD_ACOUSTIC_TOOL=ON`, build `AudioChatAecCheck`, and run it with speakers at a moderate volume:

```powershell
.\out\build\msvc-aec-release\bin\AudioChatAecCheck.exe --duration 5 --gain 0.25
```

The tool reports measured acoustic ERLE and the WebRTC AEC statistics, and exits with an error when ERLE is 10 dB or lower. Headphones cannot validate acoustic echo cancellation. The tool disables NS/AGC to isolate AEC, plays for the requested duration, and waits for each microphone frame before queuing the next reference.

The no-AEC developer build is intended for core development only. Set `aec.enable: false` in the YAML configuration before starting the engine; the default production configuration requests AEC and fails initialization when no backend is compiled. `REQUIRE_AEC=OFF` never weakens this runtime check. The no-AEC Debug and Release suites each contain 245 tests.

AEC tuning notes:

- `aec.filter_length_ms` controls reference-buffer capacity, not WebRTC's adaptive filter length.
- `aec.suppression_level` is retained for configuration compatibility; the current backend uses validated WebRTC defaults and does not change suppression strength.
- `allow_passthrough_on_error` is a C++ API option for runtime processing failures. Configuration, backend-creation and buffer-allocation failures remain fatal.

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

Confirm that `CMAKE_TOOLCHAIN_FILE` points to your vcpkg checkout and that the triplet is `x64-windows`. CMake treats PortAudio, Opus, spdlog and yaml-cpp as required; AEC production builds additionally require the `webrtc` port through the `aec` manifest feature. Missing dependencies never silently remove the executable or the required AEC backend.

### No audio device

Run `--list-devices` and set `audio.input_device` / `audio.output_device` to a listed name, or use `auto`.

### Build preset cannot find `cl.exe`

The Ninja presets require a Developer Command Prompt. The Visual Studio generator shown above does not.
