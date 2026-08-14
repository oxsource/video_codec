# Quickstart: Input Surface（Android MediaCodec 输入面）

**Branch**: `007-input-surface-api` | **Date**: 2026-08-14 | **Spec**: [spec.md](spec.md)

## 最短调用（Android，surface 输入面）

```cpp
#include "video_encoder.h"

namespace vc = video::codec;

vc::VideoConfig cfg;
cfg.codec = vc::VideoCodecType::kH264;
cfg.width = 640;
cfg.height = 480;
cfg.fps = 30;
cfg.bitrate = 2'000'000;
cfg.input_surface = true;   // surface 输入模式（须 Init 前声明）

auto enc = vc::VideoEncoder::Create(cfg);   // 平台选择：Android -> MediaCodec
enc->Init();

void* handle = enc->CreateInputSurface();   // Android 上为 ANativeWindow*（void*）
// handle == nullptr 即后端不支持（非安卓或设备不支持）

// 调用者经句柄绘制并投递（CPU 路径示例）：
//   ANativeWindow_setBuffersGeometry(win, 640, 480, WINDOW_FORMAT_RGBA_8888);
//   ANativeWindow_lock(win, &buf, nullptr);   // buf.bits / buf.stride 暴露
//   /* 在 buf.bits 中按 RGBA 绘制一帧 */
//   set_buffers_timestamp(win, pts_ns);       // dlsym 解析 ANativeWindow_setBuffersTimestamp
//   ANativeWindow_unlockAndPost(win);
//   enc->Poll();   // 泵送已就绪输出（硬件编码器输出未排空会停止消费输入面，研究 R7）

auto result = enc->Flush();   // signalEndOfInputStream + drain（带 deadline 兜底，
                              // 各厂商编码器对 EOS 输出行为不统一，框架不得死等）
// 输出经 push（SetOutputSink）或 pull（Flush 返回的包）消费，与 CPU 帧路径一致
enc->Release();
```

> 注意：surface 模式下 `Encode(VideoFrame)` 被拒绝（输入模式互斥，契约 C-012）。

## 设备验证

```sh
# 交叉编译门禁
make android-codec

# EGL 渲染进输入面 → MP4，ffprobe 双流 + ffmpeg 解码校验
make android-surface        # = bash scripts/verify/android_codec.sh surface
```

`android_codec.sh surface`：构建 → push → 设备运行 `./encode_file --surface clip 2`（SmpteBars
`Surface` 用 EGL/GLES 渲染 SMPTE 60 帧进输入面，零拷贝）→ pull
`clip-android-surface.mp4`（与 CPU 帧输出 `clip-android.mp4` 区分）→ ffprobe（h264+aac）+ 解码校验。

## 宿主机行为

非安卓构建下 `--surface` 模式不可用（`CreateInputSurface()` 返回 `nullptr`）；示例忽略该模式或
明确报"unsupported"。既有 `encode_file`（CPU 帧）与 `make host_ffmpeg_codec` 不受影响。
