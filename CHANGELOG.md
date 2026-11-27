# Changelog

## [Unreleased] - 2026-11-28

### Added

- Real WebRTC Audio Processing/AEC3 backend behind the vcpkg `aec` feature
- 10/20 ms AEC processing, delayed reference buffering and engine integration
- Synthetic ERLE, near-end preservation, double-talk and 20 ms effect tests
- Optional `AudioChatAecCheck` real-device tool with a greater-than-10 dB ERLE gate
- AUH2 wire format with sample rate, channel count and frame duration
- Headless AEC pipeline creation for offline processing and deterministic tests
- Fatal worker-error reporting and nonzero process exit status

### Fixed

- Audio streams now start with the engine; stale startup capture is discarded and the latest capture frame is used
- Incompatible peer frame durations are rejected before they can cause AEC reference underflow
- DTX and unexpected decoded frame sizes are normalized to the local frame size, with AEC resynchronization when needed
- Acoustic check reference/capture pairing no longer strands a pending reference on capture underruns
- WebRTC default high-pass filtering is retained
- Strict AEC underflow and explicit passthrough behavior are covered by real pipeline tests

### Verification

- AEC Release: 250/250 tests passed
- no-AEC Debug: 245/245 tests passed
- no-AEC Release: 245/245 tests passed
- Default AEC engine startup, 2-second run and graceful stop passed
- Real speaker/microphone acoustic ERLE: 11.15 dB over 400 measured frames

### Known limitations

- `aec.suppression_level` is reserved and does not change current AEC3 tuning
- `aec.filter_length_ms` controls reference-buffer capacity only
- GUI and full engine-side network reliability integration remain unfinished
- UDP is unencrypted and unauthenticated; trusted LAN only
- Windows x64 only

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
