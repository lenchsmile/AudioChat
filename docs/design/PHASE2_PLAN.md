# AudioChat 阶段2：音频处理增强 详细实施计划

**阶段**: 2 - 音频处理增强  
**周期**: 2-3 周  
**核心**: WebRTC AEC 回声消除集成  
**风险等级**: 🔴 高（需特别谨慎）

---

## 1. 实施策略（保守谨慎）

### 1.1 核心原则
✅ **增量实施** - 一步步进行，每步都可验证  
✅ **完全隔离** - AEC 功能独立，不影响既有功能  
✅ **充分测试** - 每个部分都有对应测试  
✅ **文档先行** - 在代码前编写接口文档  
✅ **风险回退** - 每个里程碑都可以回退  

### 1.2 实施序列

```
├─ 2.1 WebRTC 库集成 (低风险)
│  ├─ 获取和编译库
│  ├─ CMake 集成
│  └─ 链接验证
│
├─ 2.2 AecProcessor 实现 (中等风险)
│  ├─ 头文件设计
│  ├─ 基础实现
│  ├─ 参数配置
│  └─ 单元测试
│
├─ 2.3 音频缓冲管理 (中等风险)
│  ├─ 参考信号队列
│  ├─ 时序对齐
│  └─ 缓冲测试
│
├─ 2.4 AudioHandler 集成 (高风险)
│  ├─ AEC 启用/禁用
│  ├─ 参考信号采集
│  ├─ 捕获信号处理
│  └─ 集成测试
│
├─ 2.5 Codec 参数优化 (低风险)
│  ├─ DTX 启用
│  ├─ 自适应码率
│  └─ 参数测试
│
└─ 2.6 验证和调试 (中等风险)
   ├─ 功能验证
   ├─ 性能测试
   ├─ 问题排查
   └─ 文档完成
```

---

## 2. 详细实施步骤

### 2.1 WebRTC 库集成 (第1-2天)

#### 2.1.1 获取 WebRTC Audio Processing

**选项评估**:
- ✅ **推荐**: webrtc-audio-processing（轻量级）
- ⚠️ **备选**: 完整 WebRTC（体积大）

```bash
# 克隆轻量级版本
git clone https://gitlab.freedesktop.org/pulseaudio/webrtc-audio-processing.git \
  thirdparty/webrtc-audio-processing

# 或通过 vcpkg
vcpkg install webrtc-audio-processing:x64-windows
```

**验收标准**:
- ✅ 源代码/库文件可用
- ✅ 头文件可访问
- ✅ 编译依赖明确

#### 2.1.2 CMake 集成

**新增文件**: `cmake/FindWebRtcAudioProcessing.cmake`

```cmake
# 查找 WebRTC Audio Processing
find_package(WebRtcAudioProcessing REQUIRED)

# 或通过 vcpkg
pkg_check_modules(WEBRTC_AP webrtc-audio-processing-1)
```

**修改**: `CMakeLists.txt`
```cmake
find_package(WebRtcAudioProcessing REQUIRED)
target_link_libraries(AudioChat PRIVATE WebRtcAudioProcessing::WebRtcAudioProcessing)
```

**验收标准**:
- ✅ CMake 配置成功
- ✅ 编译通过（仅链接，不编写代码）
- ✅ 头文件可引用

#### 2.1.3 编译验证

```bash
cmake --preset clang-debug --fresh
cmake --build --preset clang-debug

# 检查链接成功
ldd out/build/clang-debug/bin/AudioChat | grep webrtc
```

**验收标准**:
- ✅ 编译无错误
- ✅ 编译无新警告
- ✅ 链接成功

---

### 2.2 AecProcessor 实现 (第3-5天)

#### 2.2.1 头文件设计

**新增文件**: `include/AecProcessor.h`

**设计要点**:
```cpp
struct AecConfig {
    // 基础配置
    int sample_rate_hz = 48000;
    int num_channels = 1;
    int frame_size_ms = 10;  // WebRTC 推荐
    
    // 功能开关
    bool enable_aec = true;
    bool enable_ns = true;   // 噪声抑制
    bool enable_agc = true;  // 自动增益控制
    bool enable_vad = false; // 语音活性检测
    
    // 参数
    int suppression_level = 2;  // 0=Low, 1=Moderate, 2=High
};

class AecProcessor {
public:
    explicit AecProcessor(const AecConfig& config);
    ~AecProcessor();
    
    // 核心接口（三种处理模式）
    bool processFrame(
        const int16_t* render_audio,   // 参考信号
        const int16_t* capture_audio,  // 捕获信号
        int16_t* output,               // 输出
        size_t samples_per_channel
    );
    
    bool analyzeRenderFrame(
        const int16_t* render_audio,
        size_t samples_per_channel
    );
    
    bool processCaptureFrame(
        const int16_t* capture_audio,
        int16_t* output,
        size_t samples_per_channel
    );
    
    // 配置接口
    void setSuppressionLevel(int level);
    bool isEnabled() const;
    
    // 禁用复制/移动
    AecProcessor(const AecProcessor&) = delete;
    AecProcessor(AecProcessor&&) = delete;
    
private:
    std::unique_ptr<webrtc::AudioProcessing> apm_;
    AecConfig config_;
    std::vector<int16_t> render_buffer_;
    std::vector<int16_t> capture_buffer_;
};
```

**验收标准**:
- ✅ 头文件完整无依赖问题
- ✅ 接口设计清晰
- ✅ 注释详尽

#### 2.2.2 基础实现

**新增文件**: `src/AecProcessor.cpp`

**实现步骤**:

1. **构造函数**: 初始化 AudioProcessing
```cpp
AecProcessor::AecProcessor(const AecConfig& config) : config_(config) {
    webrtc::AudioProcessing::Config apm_config;
    
    // 配置 AEC
    apm_config.echo_canceller.enabled = config.enable_aec;
    
    // 配置 NS
    apm_config.noise_suppression.enabled = config.enable_ns;
    apm_config.noise_suppression.level = 
        static_cast<webrtc::AudioProcessing::Config::NoiseSuppression::Level>(
            config.suppression_level
        );
    
    // 配置 AGC
    apm_config.gain_controller1.enabled = config.enable_agc;
    
    // 创建实例
    apm_ = webrtc::AudioProcessingBuilder()
        .SetConfig(apm_config)
        .Create();
    
    if (!apm_) {
        throw std::runtime_error("Failed to create AudioProcessing");
    }
    
    // 初始化流
    webrtc::StreamConfig stream_config(
        config.sample_rate_hz,
        config.num_channels,
        false
    );
    apm_->Initialize(stream_config, stream_config);
}
```

2. **分析参考信号**
```cpp
bool AecProcessor::analyzeRenderFrame(
    const int16_t* render_audio,
    size_t samples_per_channel
) {
    // 处理帧分割（WebRTC 需要 10ms 帧）
    size_t samples_per_10ms = config_.sample_rate_hz / 100;
    
    for (size_t i = 0; i < samples_per_channel; i += samples_per_10ms) {
        // ... 处理逻辑
    }
    
    return true;
}
```

3. **处理捕获信号**
```cpp
bool AecProcessor::processCaptureFrame(
    const int16_t* capture_audio,
    int16_t* output,
    size_t samples_per_channel
) {
    // 复制到输出
    std::copy(capture_audio, capture_audio + samples_per_channel, output);
    
    // 处理（帧分割）
    // ... 处理逻辑
    
    return true;
}
```

**验收标准**:
- ✅ 编译通过（无编译错误）
- ✅ 无运行时崩溃
- ✅ 内存无泄漏

#### 2.2.3 单元测试

**新增文件**: `tests/test_aec_processor.cpp`

```cpp
class AecProcessorTest : public ::testing::Test {
protected:
    void SetUp() override {
        Logger::init("test_logs", "error");
    }
    
    void TearDown() override {
        Logger::shutdown();
        std::filesystem::remove_all("test_logs");
    }
};

TEST_F(AecProcessorTest, Initialization) {
    AecConfig config;
    EXPECT_NO_THROW({
        AecProcessor aec(config);
    });
}

TEST_F(AecProcessorTest, ProcessFrame) {
    AecConfig config;
    AecProcessor aec(config);
    
    int samples = 480;  // 10ms @48kHz
    std::vector<int16_t> render(samples, 0);
    std::vector<int16_t> capture(samples, 0);
    std::vector<int16_t> output(samples, 0);
    
    EXPECT_TRUE(aec.processFrame(
        render.data(),
        capture.data(),
        output.data(),
        samples
    ));
}
```

**验收标准**:
- ✅ 所有测试通过
- ✅ 无内存泄漏
- ✅ 覆盖率 > 80%

---

### 2.3 音频缓冲管理 (第6-7天)

#### 2.3.1 参考信号队列

**修改**: `include/AudioHandler.h`

```cpp
class AudioHandler {
private:
    // 参考信号缓冲（播放的音频）
    SpscFixedSlots<int16_t> reference_queue_;
    std::unique_ptr<AecProcessor> aec_processor_;
    bool aec_enabled_ = false;
    
public:
    void enableAec(const AecConfig& config);
    void disableAec();
};
```

#### 2.3.2 时序对齐

**新增**: 参考信号延迟补偿

```cpp
// 播放延迟 = 播放缓冲(20ms) + 声学路径(0-20ms)
const int REFERENCE_DELAY_MS = 30;

class ReferenceSignalBuffer {
private:
    RingBuffer<int16_t> history_;
    size_t delay_samples_;
    
public:
    int16_t* getDelayedReference();
};
```

**验收标准**:
- ✅ 参考信号正确缓冲
- ✅ 延迟补偿无错误
- ✅ 缓冲测试通过

---

### 2.4 AudioHandler 集成 (第8-10天)

#### 2.4.1 修改 play() 方法

**原始**:
```cpp
void AudioHandler::play(const int16_t* data, int32_t data_size) {
    if (recording) {
        playback_queue_.tryWriteFrom(data);
    }
    PaError err = Pa_WriteStream(output_stream_, data, data_size);
}
```

**改进（保存参考信号）**:
```cpp
void AudioHandler::play(const int16_t* data, int32_t data_size) {
    // 1. 保存参考信号用于 AEC
    if (aec_enabled_) {
        reference_queue_.tryWriteFrom(data);
    }
    
    // 2. 记录播放数据用于录音
    if (recording) {
        playback_queue_.tryWriteFrom(data);
    }
    
    // 3. 播放
    PaError err = Pa_WriteStream(output_stream_, data, data_size);
    if (err != paNoError) {
        LOG_ERROR("Pa_WriteStream failed: {}", Pa_GetErrorText(err));
    }
}
```

**验收标准**:
- ✅ 参考信号正确保存
- ✅ 播放功能不受影响
- ✅ 无额外延迟

#### 2.4.2 修改 swapCaptureBuffer() 方法

**原始**:
```cpp
void AudioHandler::swapCaptureBuffer(int16_t*& capture_buffer_ptr) {
    std::swap(capture_buffer_ptr, capture_buffer_ptr_);
}
```

**改进（应用 AEC）**:
```cpp
void AudioHandler::swapCaptureBuffer(int16_t*& capture_buffer_ptr) {
    if (aec_enabled_ && aec_processor_) {
        // 1. 获取参考信号
        int16_t* reference_ptr = reference_queue_.tryAcquireRead();
        
        if (reference_ptr) {
            // 2. 应用 AEC
            int16_t* temp_buffer = reinterpret_cast<int16_t*>(
                malloc(output_frames_per_buffer_ * sizeof(int16_t))
            );
            
            bool success = aec_processor_->processFrame(
                reference_ptr,
                capture_buffer_ptr_,
                temp_buffer,
                output_frames_per_buffer_
            );
            
            reference_queue_.commitRead();
            
            if (success) {
                std::swap(temp_buffer, capture_buffer_ptr_);
            } else {
                LOG_WARN("AEC processing failed");
            }
            
            free(temp_buffer);
        } else {
            // 3. 无参考信号，仅处理捕获（NS + AGC）
            int16_t* temp_buffer = reinterpret_cast<int16_t*>(
                malloc(output_frames_per_buffer_ * sizeof(int16_t))
            );
            
            aec_processor_->processCaptureFrame(
                capture_buffer_ptr_,
                temp_buffer,
                output_frames_per_buffer_
            );
            
            std::swap(temp_buffer, capture_buffer_ptr_);
            free(temp_buffer);
        }
    }
    
    std::swap(capture_buffer_ptr, capture_buffer_ptr_);
}
```

**验收标准**:
- ✅ AEC 正确应用
- ✅ 无崩溃
- ✅ 音频质量改善可验证

#### 2.4.3 集成测试

**新增文件**: `tests/test_aec_integration.cpp`

```cpp
class AecIntegrationTest : public ::testing::Test {
    // 测试 AEC 集成到 AudioHandler
    // 验证参考信号和捕获信号的处理
};
```

**验收标准**:
- ✅ 集成测试通过
- ✅ 无音频流中断
- ✅ 回声明显降低

---

### 2.5 Codec 参数优化 (第11天)

#### 2.5.1 启用 DTX（静音检测）

**修改**: `src/AudioChatEngine.cpp` initComponents()

```cpp
// 启用 DTX（节省带宽）
int err = opus_encoder_ctl(codec_->getEncoder(), OPUS_SET_DTX(1));
if (err == OPUS_OK) {
    LOG_INFO("Opus DTX enabled");
} else {
    LOG_WARN("Failed to enable Opus DTX: {}", opus_strerror(err));
}
```

#### 2.5.2 自适应码率

```cpp
// 启用自适应码率
opus_encoder_ctl(codec_->getEncoder(), OPUS_SET_BITRATE(OPUS_AUTO));
opus_encoder_ctl(codec_->getEncoder(), OPUS_SET_VBR(1));
opus_encoder_ctl(codec_->getEncoder(), OPUS_SET_VBR_CONSTRAINT(0));
```

**验收标准**:
- ✅ 编译通过
- ✅ DTX 正常工作
- ✅ 码率自适应

---

### 2.6 验证和调试 (第12-15天)

#### 2.6.1 功能验证

```bash
# 1. 编译
cmake --preset clang-debug --fresh
cmake --build --preset clang-debug

# 2. 运行测试
ctest --preset clang-debug -V

# 3. 检查内存
valgrind ./out/build/clang-debug/bin/AudioChat
```

#### 2.6.2 性能测试

- 👂 CPU 占用 < 15%
- 📊 额外延迟 < 10ms
- 🔊 ERLE > 20dB (回声消除增强)
- 🎚️ 残留回声 < -30dB

#### 2.6.3 问题排查清单

| 问题 | 排查方法 | 解决方案 |
|------|---------|---------|
| 编译失败 | 检查 WebRTC 库路径 | 重新链接 CMake |
| 运行崩溃 | 添加 try-catch | 检查边界条件 |
| 音质差 | 检查参数配置 | 调整 AEC 级别 |
| 延迟增加 | 性能分析 | 优化算法或缓冲 |
| 内存泄漏 | Valgrind 检测 | 检查指针管理 |

**验收标准**:
- ✅ 所有功能测试通过
- ✅ 性能指标达标
- ✅ 无已知 bug

---

## 3. 风险管理

### 3.1 高风险点识别

| # | 风险 | 影响 | 概率 | 缓解 |
|----|------|------|------|------|
| 1 | WebRTC 编译失败 | 无法进行 | 中 | 使用预编译库 |
| 2 | AEC 参数不匹配 | 效果差 | 中 | 提供多级配置 |
| 3 | 时序对齐问题 | 回声反弹 | 中 | 固定延迟补偿 |
| 4 | 内存泄漏 | 崩溃 | 低 | 单元测试 + Valgrind |
| 5 | 性能下降 | 延迟增加 | 低 | 性能分析工具 |

### 3.2 回退计划

```
如果 2.4 AudioHandler 集成失败：
  → 回退到 2.2（AecProcessor 单独工作）
  → 保留 play() 和 swapCaptureBuffer() 原始代码
  → 重新设计集成方案

如果 2.2 AecProcessor 实现失败：
  → 回退到 2.1（仅库集成）
  → 使用简化的 AEC 配置
  → 重新实现处理逻辑

如果 2.1 库集成失败：
  → 使用预编译二进制
  → 或切换到其他 AEC 库
  → 或推迟 AEC 集成
```

### 3.3 验证检查点

每个步骤完成后必须验证：

```bash
✅ Step 2.1: 链接成功，无编译警告
✅ Step 2.2: 单元测试全过，内存无泄漏
✅ Step 2.3: 缓冲测试通过，时序正确
✅ Step 2.4: 集成测试通过，音频流连续
✅ Step 2.5: Codec 参数生效，码率正确
✅ Step 2.6: 性能指标达标，问题清零
```

---

## 4. 时间表

| 周期 | 步骤 | 目标 | 状态 |
|------|------|------|------|
| 第1-2天 | 2.1 | WebRTC 库集成 | ⏳ |
| 第3-5天 | 2.2 | AecProcessor 实现 | ⏳ |
| 第6-7天 | 2.3 | 音频缓冲管理 | ⏳ |
| 第8-10天 | 2.4 | AudioHandler 集成 | ⏳ |
| 第11天 | 2.5 | Codec 参数优化 | ⏳ |
| 第12-15天 | 2.6 | 验证和调试 | ⏳ |
| **总计** | | | **15 天** |

---

## 5. 交付物清单

### 代码
- [ ] AecProcessor 类 (头+源)
- [ ] ReferenceSignalBuffer 类 (可选)
- [ ] AudioHandler 修改
- [ ] AudioChatEngine 修改
- [ ] CMake 配置

### 测试
- [ ] test_aec_processor.cpp
- [ ] test_aec_integration.cpp
- [ ] 性能测试脚本

### 文档
- [ ] AEC 集成指南
- [ ] 配置说明
- [ ] 性能报告
- [ ] PHASE2_IMPLEMENTATION.md

---

## 6. 检查清单

**开始前**:
- [ ] 阶段1 100% 完成
- [ ] 代码库干净（无未提交改动）
- [ ] 分支最新（pull --rebase）
- [ ] 编译通过

**进行中**:
- [ ] 每个小步骤都编译测试
- [ ] 每天提交进度
- [ ] 遇到问题及时记录

**完成后**:
- [ ] 所有测试通过
- [ ] 文档完整
- [ ] 性能达标
- [ ] 准备合并到 develop

---

**注**: 此计划可根据实际情况调整，优先级：**稳定性 > 性能 > 功能完整性**
