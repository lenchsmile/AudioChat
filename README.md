# AudioChat

AudioChat 是一个基于 C++20、PortAudio、Opus 和 WebRTC Audio Processing 的 Windows P2P 音频通信项目。

> **当前版本：0.5.0 开发基线。**
> 阶段 0A/0B（可复现构建、内存/并发/网络安全）和阶段 2（真实 WebRTC AEC、参考时序、客观效果验收）已完成。
> 首发范围限定为 Windows 控制台和受信局域网/隔离内网；GUI、跨平台和公网安全传输不属于首发范围。

## 已实现

- 统一的 `audiochat_core` 静态库和 `AudioChat` 控制台程序
- 安全的 UDP 报文格式、长度校验、来源校验和固定容量 JitterBuffer
- 基于 SPSC 队列的音频采集、可中断的线程退出和 PortAudio RAII 管理
- YAML 配置、三个质量预设、配置往返和事务式加载
- 真实 WebRTC AEC3 后端，支持 10/20 ms 分帧、参考信号延迟缓冲、NS 和 AGC
- 启动时延迟音频流、清理旧采集帧，并始终使用最新采集帧
- AUH2 协议携带帧长；采样率、声道数或帧长不匹配的报文会被拒绝
- AEC 默认严格失败；仅代码 API 的显式降级配置允许 passthrough
- 工作线程致命错误会传递为非零进程退出码
- 250 项 AEC Release 测试；245 项 no-AEC Debug/Release 测试

## AEC 效果验收

- 合成延迟回声：测量 ERLE 大于 8 dB
- 双讲：近端语音保真度相关性大于 0.6，并允许 APM 处理时延
- 20 ms 分帧：测量 ERLE 大于 8 dB
- 真实扬声器/麦克风：5 秒、增益 0.25 的受控噪声测试，在本机测得 **11.15 dB**；工具将低于 10 dB 判为失败

AEC 效果受设备、音量、房间声学和个人配置影响，部署前应在目标环境复测。

## 构建

依赖由仓库根目录的 `vcpkg.json` 管理。生产/验收构建必须启用 `aec` feature：

```powershell
$env:VCPKG_ROOT = "C:\path\to\vcpkg"

cmake --preset msvc-aec-release
cmake --build --preset msvc-aec-release --config Release --parallel
ctest --preset msvc-aec-release -C Release --output-on-failure --timeout 60 -j1
```

该 preset 设置 `VCPKG_MANIFEST_FEATURES=aec` 和 `REQUIRE_AEC=ON`；WebRTC 后端不可用时配置直接失败。

no-AEC 构建仅用于核心开发。默认配置请求 AEC，因此在 no-AEC 构建中运行前必须显式设置 `aec.enable: false`：

```powershell
cmake -S . -B out/build/msvc-phase0 -G "Visual Studio 18 2026" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DBUILD_TESTING=ON -DREQUIRE_AEC=OFF
```

## 运行

```powershell
.\out\build\msvc-aec-release\bin\Release\AudioChat.exe --check
.\out\build\msvc-aec-release\bin\Release\AudioChat.exe --list-devices
.\out\build\msvc-aec-release\bin\Release\AudioChat.exe --config .\config\default.yaml --duration 10
```

支持 `--preset low-latency|balanced|high-quality`、`--version` 和 `--help`。

通信双方必须使用相同的采样率、声道数和帧长（10 或 20 ms）。AUH2 与旧 AUH1 报文不兼容。

## AEC 配置说明

- `aec.enable: true` 是默认值；no-AEC 构建会严格拒绝启动。
- `aec.filter_length_ms` 当前仅控制参考信号缓冲容量，不直接设置 WebRTC 自适应滤波器长度。
- `aec.suppression_level` 为兼容性保留字段；当前后端固定使用经过验证的 WebRTC 默认 AEC3 参数，不改变抑制强度。
- `allow_passthrough_on_error` 只通过 C++ API 显式启用，覆盖运行时处理失败；配置错误、后端创建失败和参考缓冲分配失败仍为致命错误。

## 安装

```powershell
cmake --install out/build/msvc-aec-release --config Release --prefix .\dist\audiochat
```

安装包包含可执行文件、运行时 DLL 和 YAML 配置。

## 尚未完成

- 网络可靠性组件在引擎中的完整闭环集成
- Qt GUI 构建和引擎连接
- DTLS/SRTP、身份认证和公网安全部署
- Linux/macOS 支持

## 安全限制

当前 UDP 音频没有加密、完整性保护或身份认证。只能部署在受信局域网或隔离内网，不得暴露到公网。参见 `LIMITATIONS.md`。

## 文档

- `BUILD.md`：完整构建、测试和真实声学验证说明
- `LIMITATIONS.md`：当前限制和验证结果
- `CHANGELOG.md`：版本变更
