# Changelog

## [0.5.0] - 2026-11-27

### Added

- vcpkg manifest with pinned baseline and required dependencies
- Unified `audiochat_core` target, console executable and registered test target
- YAML configuration with low-latency, balanced and high-quality presets
- Console CLI for configuration checks, device listing, presets and timed runs
- Safe versioned UDP packet format with payload and source validation
- Fixed-capacity JitterBuffer with stop, reorder and capacity tests
- Network, JitterBuffer and engine boundary regression tests
- CMake install rules for runtime DLLs and configuration files

### Fixed

- `Result<T>` now supports move-only values
- Missing Opus include in `CodecOptimizer.h`
- ConfigManager const error and incomplete YAML round-trip
- CMake no longer silently skips the main target when dependencies are missing
- Custom CMake Find modules are no longer ignored by Git
- NetworkMonitor recursive-lock deadlock and loss calculation
- JitterBuffer shared static lock, unlock-after-use and unsafe memcpy
- Codec 10/20 ms frame handling and Opus decode size units
- Audio callback instance isolation, capture capacity and default device selection
- Engine shutdown blocking, partial thread-start cleanup and detached-thread hazards
- Logger file-handle lifetime and thread-safe initialization
- Heartbeat callback reentrancy and interruptible backoff
- Real PortAudio device enumeration in AudioDeviceMonitor

### Verification

- Debug: 238/238 tests passed
- Release: 238/238 tests passed
- `AudioChat.exe --check` passed
- `AudioChat.exe --list-devices` passed
- Real audio engine startup, 2-second run and graceful stop passed

### Known limitations

- WebRTC AEC is not implemented yet
- GUI is not integrated
- UDP is unencrypted and unauthenticated; trusted LAN only
- Windows x64 only
