# AudioChat 修复/增强行动方案

**基于**: AUDIT_REPORT.md 审查结论  
**目标**: 使系统达到生产就绪状态  
**时间框架**: 2-8周

---

## 快速摘要

**当前状态**: 70% 代码完成，30% 实际集成，40% 可用性  
**核心问题**: WebRTC AEC未实现、新架构未使用、GUI未集成  
**修复策略**: 分三个阶段（快速修复→核心实现→质量提升）

---

## 阶段1: 快速修复（1周）- 让系统可运行

### 目标
使现有代码实际工作，完成基本集成

### 任务清单

#### 1.1 Main入口迁移（优先级: P0, 2天）

**文件**: `src/main_refactored.cpp`（新建）

**任务**:
```cpp
// 创建新的main入口使用AudioChatEngine
// 见 AUDIT_REPORT.md 附录A.1 完整代码

关键步骤:
1. 创建 main_refactored.cpp
2. 实现配置转换函数 convertConfig()
3. 实现信号处理 signalHandler()
4. 更新 CMakeLists.txt 添加新可执行文件:
   add_executable(AudioChatRefactored src/main_refactored.cpp ...)
```

**验证**:
- [ ] 编译通过
- [ ] 能启动和停止
- [ ] 优雅退出（Ctrl+C）
- [ ] 日志输出正常

---

#### 1.2 配置文件创建（优先级: P1, 1天）

**文件**: 
- `config/default.yaml`（新建）
- `config/presets/low-latency.yaml`（新建）
- `config/presets/balanced.yaml`（新建）
- `config/presets/high-quality.yaml`（新建）

**任务**:
使用 AUDIT_REPORT.md 附录A.3 的模板创建配置文件

**验证**:
- [ ] ConfigManager::loadFromFile() 成功
- [ ] 3个预设可加载
- [ ] 配置验证通过

---

#### 1.3 网络层集成（优先级: P1, 2天）

**文件**: 
- `include/AudioChatEngine.h`（修改）
- `src/AudioChatEngine.cpp`（修改）

**任务**:
```cpp
// AudioChatEngine.h 添加成员
class AudioChatEngine {
private:
    // 新增网络可靠性组件
    std::unique_ptr<NetworkMonitor> network_monitor_;
    std::unique_ptr<HeartbeatManager> heartbeat_manager_;
    std::unique_ptr<CongestionController> congestion_controller_;
    std::unique_ptr<NackHandler> nack_handler_;
};

// AudioChatEngine.cpp 在构造函数中初始化
AudioChatEngine::AudioChatEngine(const AudioChatConfig& config) {
    network_monitor_ = std::make_unique<NetworkMonitor>();
    heartbeat_manager_ = std::make_unique<HeartbeatManager>(
        config.heartbeat_interval_ms
    );
    // ... 其他初始化
}

// 在 sendLoop 中集成
void AudioChatEngine::sendLoop() {
    while (running_.load()) {
        // 1. 检查拥塞
        if (congestion_controller_) {
            auto recommended_bitrate = congestion_controller_->getRecommendedBitrate();
            codec_->setBitrate(recommended_bitrate);
        }
        
        // 2. 捕获和编码
        audio_->swapCaptureBuffer(capture_buffer_ptr);
        codec_->encode(...);
        
        // 3. 发送并记录
        network_->send(packet_ptr);
        network_monitor_->recordPacketSent(
            packet_ptr->sequence, 
            packet_ptr->data_size
        );
        heartbeat_manager_->onPacketSent();
    }
}

// 在 recvLoop 中集成
void AudioChatEngine::recvLoop() {
    while (running_.load()) {
        // 1. 接收
        auto result = network_->receive();
        
        // 2. 记录统计
        if (result.isOk()) {
            network_monitor_->recordPacketReceived(
                packet.sequence,
                packet.size,
                packet.timestamp
            );
            heartbeat_manager_->onPacketReceived();
        } else {
            network_monitor_->recordPacketLoss(expected_seq);
        }
    }
}
```

**验证**:
- [ ] 编译通过
- [ ] 网络统计正常工作
- [ ] 心跳检测工作
- [ ] 拥塞控制生效

---

#### 1.4 测试验证（优先级: P1, 1天）

**任务**:
```bash
# 编译测试
cd build
cmake --build . --target all_tests

# 运行测试
ctest --output-on-failure

# 生成测试报告
ctest --output-junit test_results.xml
```

**验证**:
- [ ] 所有测试编译通过
- [ ] > 90% 测试通过
- [ ] 生成测试报告
- [ ] 关键测试（codec, network, logger）通过

---

### 阶段1 完成标准

- ✅ AudioChatRefactored 可执行文件可运行
- ✅ 配置文件可加载
- ✅ 网络统计可见
- ✅ 测试套件运行
- ✅ 更新 README.md 说明使用方法

**预期成果**: 
- 一个可工作的基本版本
- 新架构实际被使用
- 网络可靠性功能工作

---

## 阶段2: 核心实现（2-4周）- WebRTC AEC集成

### 目标
实现项目的核心价值：真正的回声消除

### 2.1 WebRTC库安装（1-2天）

**任务**:
```bash
# 方案A: 使用vcpkg（推荐）
vcpkg install webrtc-audio-processing

# 方案B: 从源码编译
git clone https://gitlab.freedesktop.org/pulseaudio/webrtc-audio-processing.git
cd webrtc-audio-processing
meson setup build --prefix=/usr/local
ninja -C build
sudo ninja -C build install
```

**验证**:
- [ ] webrtc-audio-processing 库安装成功
- [ ] CMake find_package 能找到库
- [ ] HAS_WEBRTC_AEC 宏被定义

---

### 2.2 AecProcessor实现（3-5天）

**文件**: `src/AecProcessor.cpp`（修改）

**任务**:

#### Step 1: 实现Initialize()
```cpp
#include <modules/audio_processing/include/audio_processing.h>

Result<void> AecProcessor::Initialize() {
    // 创建WebRTC配置
    webrtc::AudioProcessing::Config apm_config;
    
    // Echo Cancellation
    apm_config.echo_canceller.enabled = config_.enable_aec;
    apm_config.echo_canceller.mobile_mode = false;
    
    // Noise Suppression
    apm_config.noise_suppression.enabled = config_.enable_ns;
    apm_config.noise_suppression.level = 
        static_cast<webrtc::AudioProcessing::Config::NoiseSuppression::Level>(
            config_.ns_level
        );
    
    // Automatic Gain Control
    apm_config.gain_controller1.enabled = config_.enable_agc;
    apm_config.gain_controller1.mode = 
        webrtc::AudioProcessing::Config::GainController1::kAdaptiveDigital;
    
    // 创建AudioProcessing实例
    apm_ = webrtc::AudioProcessingBuilder()
        .SetConfig(apm_config)
        .Create();
    
    if (!apm_) {
        return Result<void>::error(
            AudioChatError::AecInitFailed,
            "Failed to create WebRTC AudioProcessing"
        );
    }
    
    // 配置流参数
    webrtc::StreamConfig stream_config(
        config_.sample_rate_hz,
        config_.num_channels,
        false  // has_keyboard
    );
    
    int err = apm_->Initialize(stream_config, stream_config);
    if (err != webrtc::AudioProcessing::kNoError) {
        return Result<void>::error(
            AudioChatError::AecInitFailed,
            "WebRTC Initialize failed: " + std::to_string(err)
        );
    }
    
    is_initialized_ = true;
    LOG_INFO("WebRTC AEC initialized: {}Hz, {} channels",
             config_.sample_rate_hz, config_.num_channels);
    
    return Result<void>::ok();
}
```

#### Step 2: 实现ProcessReferenceStream()
```cpp
Result<void> AecProcessor::ProcessReferenceStream(const AudioFrame& reference_frame) {
    if (!is_initialized_ || !apm_) {
        return Result<void>::error(
            AudioChatError::AecProcessFailed,
            "AEC not initialized"
        );
    }
    
    auto validation = ValidateAudioFrame(reference_frame);
    if (validation.isError()) {
        return validation;
    }
    
    // 配置流
    webrtc::StreamConfig config(
        config_.sample_rate_hz,
        config_.num_channels
    );
    
    // 处理反向流（播放的音频）
    // 注意: WebRTC需要非const指针，但不会修改数据
    int err = apm_->ProcessReverseStream(
        const_cast<int16_t*>(reference_frame.data.data()),
        config,
        config,
        nullptr  // 我们不需要输出
    );
    
    if (err != webrtc::AudioProcessing::kNoError) {
        LOG_WARN("ProcessReverseStream failed: {}", err);
        // 不返回错误，仅记录，继续处理
    }
    
    frames_processed_++;
    return Result<void>::ok();
}
```

#### Step 3: 实现ProcessCaptureStream()
```cpp
Result<AudioFrame> AecProcessor::ProcessCaptureStream(const AudioFrame& capture_frame) {
    if (!is_initialized_ || !apm_) {
        return Result<AudioFrame>::error(
            AudioChatError::AecProcessFailed,
            "AEC not initialized"
        );
    }
    
    auto validation = ValidateAudioFrame(capture_frame);
    if (validation.isError()) {
        return Result<AudioFrame>::error(
            validation.error(),
            validation.message()
        );
    }
    
    // 创建输出帧（拷贝输入）
    AudioFrame output = capture_frame;
    
    // 配置流
    webrtc::StreamConfig config(
        config_.sample_rate_hz,
        config_.num_channels
    );
    
    // 处理捕获流（麦克风音频）
    // WebRTC会原地修改数据
    int err = apm_->ProcessStream(
        output.data.data(),
        config,
        config,
        output.data.data()
    );
    
    if (err != webrtc::AudioProcessing::kNoError) {
        LOG_ERROR("ProcessStream failed: {}", err);
        return Result<AudioFrame>::error(
            AudioChatError::AecProcessFailed,
            "WebRTC ProcessStream error: " + std::to_string(err)
        );
    }
    
    frames_processed_++;
    return Result<AudioFrame>::ok(std::move(output));
}
```

**验证**:
- [ ] 编译通过（链接WebRTC库）
- [ ] 单元测试通过
- [ ] 能处理音频帧（不崩溃）
- [ ] 日志显示正确的处理统计

---

### 2.3 AecAudioHandler集成（2-3天）

**文件**: 
- `include/AudioChatEngine.h`（修改）
- `src/AudioChatEngine.cpp`（修改）

**任务**:

#### Step 1: 修改AudioChatEngine使用AecAudioHandler
```cpp
// AudioChatEngine.h
#include "AecAudioHandler.h"
#include "AecProcessor.h"
#include "ReferenceSignalBuffer.h"

class AudioChatEngine {
private:
    // 替换原始AudioHandler
    std::unique_ptr<AudioChat::AecAudioHandler> aec_audio_handler_;  // 新
    std::unique_ptr<AudioChat::AecProcessor> aec_processor_;  // 新
    std::unique_ptr<AudioChat::ReferenceSignalBuffer> ref_buffer_;  // 新
    
    // 移除旧的
    // std::unique_ptr<AudioHandler> audio_;
};
```

#### Step 2: 修改initComponents()
```cpp
Result<void> AudioChatEngine::initComponents() {
    // 1. 创建原始AudioHandler（旧实现）
    HANDLE capture_ready_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    auto raw_audio = std::make_unique<AudioHandler>(
        config_.sample_rate,
        config_.sample_rate,
        config_.sample_rate * config_.frame_duration_ms / 1000,
        config_.sample_rate * config_.frame_duration_ms / 1000,
        config_.input_device.c_str(),
        config_.output_device.c_str(),
        capture_ready_event
    );
    
    // 2. 创建AEC配置
    AudioChat::AecConfig aec_config;
    aec_config.sample_rate_hz = config_.sample_rate;
    aec_config.num_channels = 1;
    aec_config.frame_duration_ms = config_.frame_duration_ms;
    aec_config.enable_aec = config_.enable_aec;
    aec_config.aec_suppression_level = config_.aec_suppression_level;
    aec_config.enable_ns = true;
    aec_config.enable_agc = true;
    
    // 3. 创建AecProcessor
    auto aec_result = AudioChat::AecProcessor::Create(aec_config);
    if (aec_result.isError()) {
        LOG_ERROR("Failed to create AecProcessor: {}", aec_result.message());
        return Result<void>::error(aec_result.error(), aec_result.message());
    }
    aec_processor_ = std::move(aec_result.value());
    
    // 4. 创建ReferenceSignalBuffer配置
    AudioChat::ReferenceSignalBuffer::Config ref_config;
    ref_config.sample_rate_hz = config_.sample_rate;
    ref_config.num_channels = 1;
    ref_config.frame_duration_ms = config_.frame_duration_ms;
    ref_config.delay_ms = 30;  // 经验值
    ref_config.buffer_duration_ms = 100;
    
    // 5. 创建ReferenceSignalBuffer
    auto ref_result = AudioChat::ReferenceSignalBuffer::Create(ref_config);
    if (ref_result.isError()) {
        LOG_ERROR("Failed to create ReferenceSignalBuffer: {}", ref_result.message());
        return Result<void>::error(ref_result.error(), ref_result.message());
    }
    ref_buffer_ = std::move(ref_result.value());
    
    // 6. 创建AecAudioHandler配置
    AudioChat::AecAudioHandler::Config aec_audio_config;
    aec_audio_config.enable_aec = config_.enable_aec;
    aec_audio_config.aec_config = aec_config;
    aec_audio_config.buffer_config = ref_config;
    
    // 7. 创建AecAudioHandler（包装原始AudioHandler）
    auto aec_audio_result = AudioChat::AecAudioHandler::Create(
        std::move(raw_audio),
        aec_audio_config
    );
    if (aec_audio_result.isError()) {
        LOG_ERROR("Failed to create AecAudioHandler: {}", aec_audio_result.message());
        return Result<void>::error(aec_audio_result.error(), aec_audio_result.message());
    }
    aec_audio_handler_ = std::move(aec_audio_result.value());
    
    LOG_INFO("AEC audio pipeline initialized");
    
    // ... 继续初始化codec和network
    return Result<void>::ok();
}
```

#### Step 3: 修改sendLoop使用AEC
```cpp
void AudioChatEngine::sendLoop() {
    int input_frames_per_buffer = config_.sample_rate * config_.frame_duration_ms / 1000;
    
    while (running_.load()) {
        // 1. 获取捕获的音频（已经过AEC处理）
        auto capture_result = aec_audio_handler_->GetProcessedCaptureFrame();
        if (capture_result.isError()) {
            LOG_ERROR("Failed to get capture frame: {}", capture_result.message());
            continue;
        }
        
        auto& capture_frame = capture_result.value();
        
        // 2. 编码
        uint8_t encode_buffer[1024];
        int32_t encoded_size = 0;
        codec_->encode(
            capture_frame.data.data(),
            encode_buffer,
            sizeof(encode_buffer),
            encoded_size
        );
        
        // 3. 发送
        PPacket packet = ...;  // 填充packet
        network_->send(packet);
        
        // 4. 记录统计
        network_monitor_->recordPacketSent(...);
    }
}
```

**验证**:
- [ ] 编译通过
- [ ] 音频捕获工作
- [ ] AEC处理不崩溃
- [ ] 编码和发送正常

---

### 2.4 端到端AEC测试（2-3天）

**文件**: `tests/test_aec_e2e.cpp`（新建）

**任务**:
```cpp
// 创建端到端AEC效果测试
// 1. 播放已知音频（如正弦波）到扬声器
// 2. 同时录制麦克风
// 3. 验证捕获音频中回声被抑制

TEST(AecE2E, EchoSuppressionWorks) {
    // 1. 准备测试音频（1kHz正弦波）
    std::vector<int16_t> test_signal = generateSineWave(1000, 48000, 1.0);
    
    // 2. 创建AEC pipeline
    auto aec_config = createTestAecConfig();
    auto aec_processor = AecProcessor::Create(aec_config).value();
    
    // 3. 模拟播放（参考信号）
    AudioFrame reference_frame;
    reference_frame.data = test_signal;
    reference_frame.sample_count = test_signal.size();
    reference_frame.channels = 1;
    aec_processor->ProcessReferenceStream(reference_frame);
    
    // 4. 模拟麦克风捕获（包含回声）
    std::vector<int16_t> capture_with_echo = test_signal;  // 简化: 假设100%回声
    AudioFrame capture_frame;
    capture_frame.data = capture_with_echo;
    capture_frame.sample_count = capture_with_echo.size();
    capture_frame.channels = 1;
    
    // 5. 处理（应消除回声）
    auto result = aec_processor->ProcessCaptureStream(capture_frame);
    ASSERT_TRUE(result.isOk());
    
    auto& processed = result.value();
    
    // 6. 验证回声被抑制（能量降低）
    double input_energy = calculateEnergy(capture_with_echo);
    double output_energy = calculateEnergy(processed.data);
    
    double suppression_db = 10 * std::log10(input_energy / output_energy);
    
    // 期望至少10dB抑制
    EXPECT_GT(suppression_db, 10.0) 
        << "Echo suppression insufficient: " << suppression_db << " dB";
}
```

**验证**:
- [ ] 测试编译通过
- [ ] 能测量回声抑制（ERLE）
- [ ] ERLE > 10dB（表示AEC工作）
- [ ] 双讲测试通过

---

### 阶段2 完成标准

- ✅ WebRTC库正确集成
- ✅ AecProcessor核心功能实现
- ✅ AudioChatEngine使用AEC pipeline
- ✅ 端到端测试显示回声被抑制
- ✅ 更新文档说明AEC状态和使用方法

**预期成果**:
- 真正可用的回声消除功能
- 项目核心价值得以体现
- 音频质量显著提升

---

## 阶段3: GUI和质量提升（1-2周）

### 目标
完善GUI，提升整体质量和可用性

### 3.1 GUI集成（2-3天）

**任务**:

#### Step 1: 添加CMake配置
使用 AUDIT_REPORT.md 附录A.2 的配置

#### Step 2: 创建GUI入口
```cpp
// src/gui/main_gui.cpp（新建）
#include <QApplication>
#include "MainWindow.h"
#include "Logger.h"

int main(int argc, char *argv[]) {
    // 初始化日志
    Logger::init("logs/audiochat_gui.log", spdlog::level::info);
    
    // 创建Qt应用
    QApplication app(argc, argv);
    
    // 创建主窗口
    MainWindow window;
    window.show();
    
    // 事件循环
    return app.exec();
}
```

#### Step 3: 连接MainWindow和AudioChatEngine
```cpp
// MainWindow.cpp 添加
class MainWindow : public QMainWindow {
private:
    std::unique_ptr<AudioChatEngine> engine_;
    QTimer* stats_update_timer_;
    
    void onStartCall() {
        auto config = getConfigFromUI();
        engine_ = std::make_unique<AudioChatEngine>(config);
        
        auto init_result = engine_->init();
        if (init_result.isError()) {
            QMessageBox::critical(this, "Error", 
                QString::fromStdString(init_result.message()));
            return;
        }
        
        auto start_result = engine_->start();
        if (start_result.isError()) {
            QMessageBox::critical(this, "Error",
                QString::fromStdString(start_result.message()));
            return;
        }
        
        // 启动统计更新定时器
        stats_update_timer_->start(1000);  // 每秒更新
        is_connected_ = true;
        updateUI();
    }
    
    void onStatisticsTimerTick() {
        if (!engine_) return;
        
        // 获取网络统计
        auto metrics = engine_->getNetworkMetrics();
        
        // 更新UI
        onStatisticsUpdate(
            metrics.packet_loss_rate,
            metrics.rtt_ms,
            metrics.jitter_ms
        );
    }
};
```

**验证**:
- [ ] GUI编译通过
- [ ] 界面显示正常
- [ ] 能启动和停止通话
- [ ] 统计数据实时更新

---

### 3.2 质量改进（2-3天）

#### Task 1: 移除malloc/free
```cpp
// 修改AudioChatEngine.cpp中的内存分配
// 旧代码:
// auto packet_ptr = reinterpret_cast<PPacket>(malloc(...));

// 新代码:
class PacketBuffer {
public:
    explicit PacketBuffer(size_t max_size) {
        buffer_.resize(sizeof(Packet) + max_size);
        packet_ = reinterpret_cast<PPacket>(buffer_.data());
    }
    
    PPacket get() { return packet_; }
    
private:
    std::vector<uint8_t> buffer_;
    PPacket packet_;
};

// 在sendLoop中:
PacketBuffer packet_buffer(max_encode_size);
while (running_) {
    auto packet_ptr = packet_buffer.get();  // 复用buffer
    // ... 使用packet_ptr
}
```

#### Task 2: 跨平台API替换
```cpp
// 替换 Windows Event
// 旧代码:
// HANDLE capture_ready_event = CreateEventA(...);
// WaitForSingleObject(capture_ready_event, INFINITE);

// 新代码:
class AudioChatEngine {
private:
    std::mutex capture_mutex_;
    std::condition_variable capture_cv_;
    bool capture_ready_ = false;
};

// 在音频回调中:
{
    std::lock_guard<std::mutex> lock(capture_mutex_);
    capture_ready_ = true;
}
capture_cv_.notify_one();

// 在sendLoop中:
std::unique_lock<std::mutex> lock(capture_mutex_);
capture_cv_.wait(lock, [this] { return capture_ready_ || !running_; });
capture_ready_ = false;
```

#### Task 3: 添加性能监控
```cpp
// 创建 PerformanceMonitor.h/cpp
class PerformanceMonitor {
public:
    struct Metrics {
        double avg_cpu_percent;
        size_t memory_bytes;
        double avg_latency_ms;
    };
    
    void startFrame();
    void endFrame();
    Metrics getMetrics() const;
};

// 在AudioChatEngine中集成
void AudioChatEngine::sendLoop() {
    PerformanceMonitor perf;
    
    while (running_) {
        perf.startFrame();
        
        // ... 处理音频
        
        perf.endFrame();
        
        // 定期输出
        if (frame_count % 100 == 0) {
            auto metrics = perf.getMetrics();
            LOG_INFO("Performance: CPU={}%, Mem={}MB, Latency={}ms",
                     metrics.avg_cpu_percent,
                     metrics.memory_bytes / 1024 / 1024,
                     metrics.avg_latency_ms);
        }
    }
}
```

**验证**:
- [ ] 无malloc/free
- [ ] 跨平台编译通过（Linux测试）
- [ ] 性能指标可见

---

### 3.3 文档完善（1-2天）

**任务**:

#### Task 1: 更新README.md
```markdown
# AudioChat

## Quick Start

### Build
cmake -B build -DBUILD_GUI=ON
cmake --build build --config Release

### Run (Console)
./build/bin/AudioChatRefactored config/default.yaml

### Run (GUI)
./build/bin/AudioChatGui

## Features
- ✅ WebRTC Acoustic Echo Cancellation
- ✅ Opus codec with FEC and DTX
- ✅ Network reliability (monitoring, congestion control, NACK)
- ✅ Qt6 GUI interface
- ✅ YAML configuration with 3 quality presets

## Requirements
- PortAudio
- Opus
- spdlog
- WebRTC Audio Processing
- Qt6 (optional, for GUI)
```

#### Task 2: 创建CHANGELOG.md
```markdown
# Changelog

## [1.0.0] - 2024-XX-XX

### Added
- WebRTC AEC integration (回声消除)
- Modern C++ architecture with RAII
- Result<T> error handling
- Network monitoring and reliability
- Qt6 GUI application
- YAML configuration system
- 250+ unit and integration tests

### Fixed
- Memory leaks (replaced malloc/free with smart pointers)
- Thread safety (replaced detached threads)
- Cross-platform compatibility (removed Windows-specific APIs)

### Known Limitations
- Currently supports 1-to-1 calls only
- UDP only (no encryption)
- Requires WebRTC library manual installation
```

#### Task 3: 创建USER_GUIDE.md
```markdown
# AudioChat User Guide

## Configuration

### Audio Settings
- `sample_rate`: 8000, 16000, 32000, 48000 Hz
- `frame_duration_ms`: 10 or 20 ms (10 for lower latency)
- `input_device`: "auto" or device name
- `output_device`: "auto" or device name

### Quality Presets
1. **Low Latency** (gaming, real-time)
   - 10ms frame, 24kbps, minimal AEC
   - Latency: ~50ms
   
2. **Balanced** (default)
   - 20ms frame, 32kbps, full AEC
   - Latency: ~100ms
   
3. **High Quality** (music, podcasts)
   - 40ms frame, 64kbps, enhanced AEC
   - Latency: ~150ms

## Troubleshooting

### No audio
1. Check device names in config.yaml
2. Try "auto" for automatic selection
3. Check firewall (UDP port)

### Echo still present
1. Increase AEC suppression_level (0-2)
2. Reduce speaker volume
3. Use headphones instead of speakers

### High latency
1. Use "low-latency" preset
2. Reduce frame_duration_ms to 10
3. Check network RTT
```

**验证**:
- [ ] README完整
- [ ] CHANGELOG准确
- [ ] USER_GUIDE清晰

---

### 阶段3 完成标准

- ✅ GUI完全集成并可用
- ✅ 代码质量改进（无malloc, 跨平台）
- ✅ 性能监控完善
- ✅ 文档完整准确
- ✅ 所有测试通过

**预期成果**:
- 生产就绪的系统
- 专业的用户界面
- 完整的文档

---

## 验收标准

### 功能验收
- [ ] 基本通话功能正常
- [ ] 回声消除明显工作（主观和客观测试）
- [ ] 网络统计正确显示
- [ ] 配置系统工作
- [ ] GUI响应流畅

### 质量验收
- [ ] 所有单元测试通过 (>95%)
- [ ] 集成测试通过
- [ ] 无内存泄漏（valgrind/Dr. Memory）
- [ ] 无未定义行为（sanitizers）
- [ ] 代码覆盖率 > 70%

### 性能验收
- [ ] 端到端延迟 < 150ms (Balanced preset)
- [ ] CPU占用 < 10% (单核)
- [ ] 内存占用 < 50MB
- [ ] 5%丢包时质量可接受

### 文档验收
- [ ] README准确描述功能和限制
- [ ] BUILD.md构建说明可用
- [ ] USER_GUIDE.md使用说明清晰
- [ ] API文档（Doxygen）生成

---

## 时间表

| 阶段 | 任务 | 时间 | 累计 |
|-----|------|------|------|
| **阶段1** | 快速修复 | 1周 | 1周 |
| 1.1 | Main迁移 | 2天 | - |
| 1.2 | 配置文件 | 1天 | - |
| 1.3 | 网络集成 | 2天 | - |
| 1.4 | 测试验证 | 1天 | - |
| 1.5 | 缓冲 | 1天 | - |
| **阶段2** | WebRTC AEC | 2-4周 | 3-5周 |
| 2.1 | 库安装 | 1-2天 | - |
| 2.2 | Processor实现 | 3-5天 | - |
| 2.3 | Handler集成 | 2-3天 | - |
| 2.4 | E2E测试 | 2-3天 | - |
| 2.5 | 调优 | 3-5天 | - |
| 2.6 | 缓冲 | 2-3天 | - |
| **阶段3** | GUI和质量 | 1-2周 | 4-7周 |
| 3.1 | GUI集成 | 2-3天 | - |
| 3.2 | 质量改进 | 2-3天 | - |
| 3.3 | 文档完善 | 1-2天 | - |
| 3.4 | 最终测试 | 2-3天 | - |
| **总计** | | **4-7周** | |

---

## 风险管理

### 高风险项

#### 风险1: WebRTC编译/链接问题
**概率**: 中等  
**影响**: 高（阻塞阶段2）  
**缓解**:
- 优先使用预编译包（vcpkg）
- 准备多个安装方案
- 预留2-3天buffer时间
- 如失败，考虑使用简化AEC算法替代

#### 风险2: AEC效果不佳
**概率**: 中等  
**影响**: 中等（影响质量）  
**缓解**:
- 提供多级配置（suppression_level）
- 允许禁用AEC
- 准备降级方案（仅NS+AGC）
- 收集实际测试数据调优

#### 风险3: 时间超支
**概率**: 高  
**影响**: 中等  
**缓解**:
- 每个阶段设置缓冲时间
- 可选功能（GUI）可延后
- 增量交付（阶段1可独立发布）

---

## 资源需求

### 人力
- **主开发**: 1人全职（4-7周）
- **测试**: 0.5人（阶段2-3）
- **文档**: 0.3人（阶段3）

### 硬件
- 开发机器（Windows + Linux）
- 2套音频设备（测试AEC）
- 网络测试环境（模拟延迟/丢包）

### 软件/库
- WebRTC Audio Processing
- Qt6 (GUI)
- 音频测试工具（Audacity等）

---

## 交付物清单

### 代码
- [ ] src/main_refactored.cpp
- [ ] config/default.yaml
- [ ] config/presets/*.yaml
- [ ] 修改后的AudioChatEngine（集成网络层和AEC）
- [ ] 完整实现的AecProcessor
- [ ] GUI应用（AudioChatGui）
- [ ] 新增测试（test_aec_e2e.cpp等）

### 文档
- [ ] AUDIT_REPORT.md（本报告）
- [ ] REMEDIATION_PLAN.md（本计划）
- [ ] 更新的README.md
- [ ] CHANGELOG.md
- [ ] USER_GUIDE.md
- [ ] BUILD.md更新

### 测试
- [ ] 测试报告（test_results.xml）
- [ ] 覆盖率报告
- [ ] 性能基准测试结果
- [ ] AEC效果测试数据

---

## 后续维护计划

### 短期（3个月内）
- 收集用户反馈
- 修复发现的bug
- 性能调优
- 文档改进

### 中期（6个月内）
- 加密通信（DTLS）
- 跨平台完善（macOS）
- 性能优化（SIMD）
- 更多音频效果

### 长期（1年内）
- 多人通话支持
- 视频通话
- 云服务集成
- 移动端支持

---

## 联系和支持

**项目仓库**: [AudioChat GitHub](https://github.com/xxx/AudioChat)  
**问题反馈**: [Issues](https://github.com/xxx/AudioChat/issues)  
**技术支持**: support@audiochat.com

---

**方案版本**: 1.0  
**最后更新**: 2024年  
**批准人**: [项目负责人]  
**执行人**: [开发团队]

---

## 附录: 快速命令参考

### 编译
```bash
# 完整构建（包含GUI）
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_GUI=ON
cmake --build build --config Release -j8

# 仅控制台版本
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### 测试
```bash
# 运行所有测试
cd build
ctest --output-on-failure -j8

# 运行特定测试
./tests/test_aec_processor
./tests/test_phase2_integration
```

### 运行
```bash
# 控制台版本
./build/bin/AudioChatRefactored config/default.yaml

# GUI版本
./build/bin/AudioChatGui

# 使用预设
./build/bin/AudioChatRefactored config/presets/low-latency.yaml
```

---

**修复计划结束**
