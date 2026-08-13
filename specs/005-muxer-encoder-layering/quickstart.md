# Quickstart: Muxer 与 Encoder 分层设计

**Branch**: `005-muxer-encoder-layering` | **Date**: 2026-08-13

编码 → 队列 → 封装 → 落盘的最短接线。Muxer 实现 `PacketSink`，因此
`queue.Await(*muxer)` 直接衔接，无需消费者适配器。

```cpp
#include <video_codec/video_codec.h>
#include "io/file_byte_sink.h"
#include "queue/packet_queue.h"
#include "utils/smpte_bars.h"

namespace vc = video::codec;

// 1. 编码器配置（协议无关：只出裸码流）
vc::VideoConfig enc_cfg;
enc_cfg.codec = vc::VideoCodecType::kH264;
enc_cfg.width = 640;
enc_cfg.height = 480;
enc_cfg.fps = 30;
enc_cfg.input_format = vc::PixelFormat::kI420;
enc_cfg.backend = vc::Backend::kFFmpeg;

auto encoder = vc::CreateVideo(enc_cfg);
encoder->Init();

// 2. 队列（中转）
vc::PacketQueue queue(64, vc::Backpressure::kBlock);
encoder->SetOutputSink(&queue);  // push 模式：包自动进队

// 3. 封装器配置（v1: MP4，分片）
vc::MuxerConfig mux_cfg;
mux_cfg.format = vc::MuxFormat::kMp4;
mux_cfg.fragmented = true;
mux_cfg.width = 640;
mux_cfg.height = 480;
mux_cfg.fps = 30;

auto muxer = vc::CreateMuxer(mux_cfg);
vc::FileByteSink sink("out.mp4");   // io 层字节目标
muxer->SetOutput(&sink);            // 注入输出（须早于首次 Push）

// 4. 消费线程：队列 → 封装器（Await 直接投递，EOS 后自动 Finish）
std::thread worker([&] { queue.Await(*muxer); });

// 5. 喂帧
for (int i = 0; i < 30; ++i) {
  auto frame = vc::utils::SmpteBars::MakeFrame(640, 480, 30, i);
  encoder->Encode(frame);
}

// 6. 结束
encoder->Flush();
queue.MarkEos();
worker.join();   // Await 内部已调用 muxer->Finish()（写 MP4 尾部）
```

## 说明

- `--raw`（裸码流）场景：不建 muxer，`queue.Await(file_consumer)` 即可，与旧版
  一致。
- Muxer 在**首个关键帧**懒打开容器并一次性产出"头部 + 首分片"；先到的非关键帧丢弃。
- 输出目标可换任意 `io::ByteSink`（文件 / 网络流 / tee），`SetOutput` 一处切换。
- 编码器不感知封装：`VideoConfig` 与 `MuxerConfig` 无引用关系，接线在调用方。
