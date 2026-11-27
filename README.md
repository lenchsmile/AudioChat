# AudioChat

AudioChat 是一个基于 C++20、PortAudio 和 Opus 的 Windows P2P 音频通信项目。

> **当前版本：0.5.0 开发基线，不是生产发布版。**
> 阶段 0A（可复现构建）和阶段 0B（内存、并发、网络安全）已完成。
> WebRTC AEC 尚未实现，GUI 尚未集成，通信仅适用于受信局域网。

## 已实现

- 统一的 `audiochat_core` 静态库和 `AudioChat` 控制台程序
- 安全的 UDP 报文格式、长度校验、来源校验和固定容量 JitterBuffer
- 基于 SPSC 队列的音频采集、可中断的线程退出和 PortAudio RAII 管理
- YAML 配置、三个质量预设、配置往返和事务式加载
- 网络统计、心跳、拥塞控制、NACK 组件
- Windows MSVC Debug/Release 构建
- 238 项注册测试，Debug 和 Release 均通过

## 尚未实现

- WebRTC Audio Processing 真实 AEC、参考信号时序和 ERLE 验收
- 网络可靠性组件在引擎中的完整闭环集成
- Qt GUI 构建和引擎连接
- DTLS/SRTP、身份认证和公网安全部署
- Linux/macOS 支持

## 构建

依赖由仓库根目录的 `vcpkg.json` 管理。

```powershell
$env:VCPKG_ROOT = "C:\path\to\vcpkg"

cmake -S . -B out/build/msvc-phase0 -G "Visual Studio 18 2026" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
  -DBUILD_TESTING=ON `
  -DREQUIRE_AEC=OFF

cmake --build out/build/msvc-phase0 --config Release --parallel
ctest --test-dir out/build/msvc-phase0 -C Release --output-on-failure
```

`REQUIRE_AEC=ON` 仅在 WebRTC Audio Processing 后端可用并完成阶段 2 后使用。

## 运行

```powershell
.\out\build\msvc-phase0\bin\Release\AudioChat.exe --check
.\out\build\msvc-phase0\bin\Release\AudioChat.exe --list-devices
.\out\build\msvc-phase0\bin\Release\AudioChat.exe --config .\config\default.yaml --duration 10
```

支持 `--preset low-latency|balanced|high-quality`、`--version` 和 `--help`。

## 安装

```powershell
cmake --install out/build/msvc-phase0 --config Release --prefix .\dist\audiochat
```

安装包包含可执行文件、运行时 DLL 和 YAML 配置。

## 安全限制

当前 UDP 音频没有加密、完整性保护或身份认证。只能部署在受信局域网或隔离内网，不得暴露到公网。参见 `LIMITATIONS.md`。

## 文档

- `BUILD.md`：完整构建和测试说明
- `LIMITATIONS.md`：当前限制和未完成功能
- `CHANGELOG.md`：版本变更
- `AUDIT_REPORT.md`、`REMEDIATION_PLAN.md`、`审查总结.md`：历史审查材料
