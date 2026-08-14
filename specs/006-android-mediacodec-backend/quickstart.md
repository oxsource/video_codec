# Quickstart: Android MediaCodec 后端实现

**Branch**: `006-android-mediacodec-backend` | **Date**: 2026-08-14

在 Android 上运行与桌面完全相同的编码/封装流程。`Backend::kAuto` 在 Android 构建
自动解析为 `kAndroid`，代码无需平台分支。

## 前置条件

- Android NDK（本地：`ANDROID_NDK_HOME=/Users/moks/Library/Android/sdk/ndk/25.2.9519653`）
- NDK 仓库已注册（`codec/WORKSPACE` 中 `android_ndk_repository(name = "androidndk")`，本功能接线项）

## 构建（交叉编译门禁）

```bash
cd codec
make android-verify          # 交叉编译 gate：spike + 后端目标，宿主不链接 NDK
# 或显式（--config 同时应用 --platforms、--extra_toolchains 与 cc-toolchain resolution）：
bazel build //src/examples:encode_file --config android_arm64
```

宿主（非 Android）构建不受影响：

```bash
bazel test //tests/...       # 既有测试（16 项）保持通过
bazel run //src/examples:encode_file -- out 3   # 宿主默认 backend=auto → FFmpeg，输出 out-ffmpeg.mp4
```

## 设备/模拟器运行

将交叉编译出的示例二进制推送至设备并运行（与 `mediacodec_spike` 同验证模式）：
`--backend` 缺省为 `auto`（Android 上自动解析为 mediacodec），输出文件名带
`-<backend>` 后缀（`BackendToString` 规范名）：

```bash
adb push <bazel-bin>/src/examples/encode_file /data/local/tmp/
adb shell /data/local/tmp/encode_file clip 3          # → clip-android.mp4
adb pull /data/local/tmp/clip-android.mp4 .
# 校验输出含 H.264 + AAC 双轨：
ffprobe -show_streams clip-android.mp4
```

## 最短调用（与桌面一致，无平台分支）

```cpp
#include <video_codec/video_codec.h>
#include "io/file_byte_sink.h"
#include "queue/packet_queue.h"
#include "utils/smpte_bars.h"

namespace vc = video::codec;

vc::PacketQueue queue(64, vc::Backpressure::kBlock);

// 编码器：kAuto —— Android 上解析为 kAndroid（系统硬件编码）
vc::VideoConfig vcfg;
vcfg.codec = vc::VideoCodecType::kH264;
vcfg.width = 640; vcfg.height = 480; vcfg.fps = 30;
vcfg.input_format = vc::PixelFormat::kI420;
vcfg.backend = vc::Backend::kAuto;
auto venc = vc::CodecFactory::CreateVideo(vcfg, &queue);  // create+Init+push 接线

vc::AudioConfig acfg;
acfg.codec = vc::AudioCodecType::kAAC;
acfg.sample_rate = 48000; acfg.channels = 2;
acfg.backend = vc::Backend::kAuto;
auto aenc = vc::CodecFactory::CreateAudio(acfg, &queue);

// 封装器：kAuto —— Android 上为 MediaMuxer 后端
vc::MuxerConfig mcfg;
mcfg.format = vc::MuxFormat::kMp4;
mcfg.width = 640; mcfg.height = 480; mcfg.fps = 30;
mcfg.audio_codec = vc::AudioCodecType::kAAC;
mcfg.sample_rate = 48000; mcfg.channels = 2;
auto muxer = vc::CodecFactory::CreateMuxer(mcfg);
auto sink = std::make_unique<vc::FileByteSink>("clip.mp4");
muxer->SetOutput(sink.get());

// 消费线程：Await 直接投递（视频+音频）到 muxer，EOS 后自动 Finish
std::thread worker([&] { queue.Await(*muxer); });

// 喂帧（SMPTE 画面 + 测试音，pacer 按视频帧时长补音频）
vc::utils::SmpteBars::AudioPace pace(vc::utils::SmpteBars::AudioOptions(), 30);
for (int i = 0; i < 90; ++i) {
  venc->Encode(vc::utils::SmpteBars::MakeVideoFrame(640, 480, 30, i));
  vc::AudioFrame af;
  while (pace.NextAudioFrame(i, &af)) aenc->Encode(af);
}

venc->Flush(); aenc->Flush();
queue.MarkEos();
worker.join();  // muxer->Finish() 内部已回放字节到 sink
```

## 行为差异提示

- Android 封装后端（MediaMuxer）在 `Finish()` 时**一次性**产出全部字节（非分片增量）；
  桌面 FFmpeg 封装为分片增量。二者输出均为合法 MP4，媒体内容一致。
