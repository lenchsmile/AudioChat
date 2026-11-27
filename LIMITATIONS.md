# AudioChat 0.5.0 Limitations

AudioChat 0.5.0 is a development baseline. The Windows console/trusted-LAN AEC scope has completed stage 2 validation, but the items below remain outside that scope or unfinished.

## Security

- UDP audio is not encrypted or authenticated.
- There is no DTLS/SRTP handshake, key verification or replay protection.
- Only isolated, trusted LAN deployments are in scope.
- Do not expose the application to the public Internet.

## Audio processing

- Real WebRTC AEC is available only when the vcpkg `aec` feature is enabled and `REQUIRE_AEC=ON` is used for production builds.
- no-AEC builds are developer-only passthrough builds. The default AEC-enabled configuration fails strictly; disabling AEC must be explicit.
- Runtime AEC failures are fatal by default. The C++ API option `allow_passthrough_on_error` permits degraded processing only when explicitly selected; configuration, backend-creation and buffer-allocation errors remain fatal.
- `aec.filter_length_ms` controls reference-buffer capacity, not the WebRTC adaptive filter length.
- `aec.suppression_level` is a reserved compatibility field. Current AEC3 tuning uses validated WebRTC defaults and does not vary with this field.
- Peers must use the same sample rate, channel count and 10/20 ms frame duration. AUH2 rejects incompatible formats and is not compatible with AUH1.
- Acoustic performance depends on the device, playback volume and room. Validate the target environment before deployment.

## Networking

- `NetworkMonitor`, heartbeat and congestion components exist and are tested independently.
- Full engine-side reliability integration and NACK wire protocol are not complete.
- Packet payloads are validated for length, format, frame duration and source, but the protocol has no cryptographic integrity.

## Platform and UI

- Windows x64 only.
- Qt GUI source exists but is not built or connected to the engine.
- Linux and macOS are not supported in this release.

## Validation completed

- AEC Release: 250/250 registered GoogleTest cases passed.
- no-AEC Debug: 245/245 registered GoogleTest cases passed.
- no-AEC Release: 245/245 registered GoogleTest cases passed.
- Synthetic delayed-echo ERLE, near-end preservation, double-talk and 20 ms processing tests passed.
- Strict no-backend failure and explicit no-AEC startup paths passed.
- Default AEC engine startup, 2-second run and graceful stop passed.
- Real speaker/microphone check on 2026-11-28: 11.15 dB acoustic ERLE over 400 measured frames; acceptance threshold is greater than 10 dB.
- Malformed packet, frame-duration mismatch, source, capacity, reordering and shutdown tests passed.
