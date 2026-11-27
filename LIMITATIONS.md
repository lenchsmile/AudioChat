# AudioChat 0.5.0 Limitations

AudioChat 0.5.0 is a development baseline, not a production release.

## Security

- UDP audio is not encrypted or authenticated.
- There is no DTLS/SRTP handshake, key verification or replay protection.
- Only isolated, trusted LAN deployments are in scope.
- Do not expose the application to the public Internet.

## Audio processing

- WebRTC Audio Processing is not integrated yet.
- `AecProcessor` remains an interface-level placeholder in no-AEC developer builds.
- AEC reference timing, ERLE measurement and double-talk acceptance are not complete.
- The engine currently sends captured audio without AEC processing.

## Networking

- `NetworkMonitor`, heartbeat and congestion components exist and are tested independently.
- Full engine-side reliability integration and NACK wire protocol are not complete.
- Packet payloads are validated for length, format and source, but the protocol has no cryptographic integrity.

## Platform and UI

- Windows x64 only.
- Qt GUI source exists but is not built or connected to the engine.
- Linux and macOS are not supported in this release.

## Validation completed

- MSVC Debug and Release builds
- 238 registered GoogleTest cases passing in both configurations
- Malformed packet, capacity, reordering and shutdown tests
- Real audio device startup and graceful 2-second shutdown smoke test
