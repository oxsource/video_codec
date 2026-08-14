# Android Backend Contract: MediaCodec 后端实现

**Branch**: `006-android-mediacodec-backend` | **Date**: 2026-08-14 | **Spec**: [spec.md](../spec.md) | **Data model**: [data-model.md](../data-model.md)

> 本文档定义 Android 后端（`backend/android`）对框架的**实现契约**与对外**行为契约**，供实现与验收对照。

## 1. 注册与选择契约

- **C-001**: 后端在 `backend/android/register.cc` 静态初始化注册 `CodecFactory::RegisterVideo/Audio/Muxer(Backend::kAndroid, ...)`；该 target `alwayslink = True`（否则静态初始化被丢弃，`Create*` 返回空——同 FFmpeg 先例）。
- **C-002**: `Backend::kAuto` 的 Android 构建解析为 `kAndroid`（`ResolveBackend` 既有行为）；宿主构建显式请求 `kAndroid` 时 `Create*` 返回空 / `Create*(cfg, sink)` 返回 `kPlatformUnsupported`，不产生损坏输出。
- **C-003**: 后端仅参与 Android 目标平台构建（`target_compatible_with = ["@platforms//os:android"]` + `select()`），宿主保持 NDK-free。
- **C-004**: 后端只依赖 `core`/`api`/`utils`/`mediacodec_utils` 与 libmediandk；不得引入 FFmpeg 或其他后端（架构不变式）。

## 2. 视频编码契约（MediaCodecVideoEncoder）

- **C-010**: 支持 `VideoCodecType::kH264`（必备）；`kHEVC` 设备支持时可用，设备不支持返回 `kPlatformUnsupported`（不静默降级）。
- **C-011**: CPU 输入路径：`Encode(VideoFrame)` 将帧数据按 `input_format`（I420/NV12）映射为编码器 `COLOR_FORMAT`（I420→Planar(19)，NV12→SemiPlanar(21)）并拷贝入输入缓冲；不支持格式返回 `kUnsupportedFormat`。
- **C-012**: `Encode(NativeBuffer)` 返回 `kUnsupportedOperation`（v1 无零拷贝路径）；`CreateInputSurface()` 返回空。
- **C-013**: 输出为 Annex-B：`data` 含起始码；关键帧包**自带** SPS/PPS（本平台 codec 关键帧 payload 已含 `[sc]SPS [sc]PPS [sc]IDR`，实测），仅当 payload 首个 NAL 非 SPS（部分设备把 SPS/PPS 只放 CODEC_CONFIG 缓冲）时才预置 `sps_/pps_`（自 `BUFFER_FLAG_CODEC_CONFIG` 缓冲提取缓存）；`keyframe` 由 `BUFFER_FLAG_KEY_FRAME` 决定。
- **C-014**: `pts_us` 与输入帧时间戳一致（微秒）；包序与输入序一致；push 模式下包经 `PacketSink::Push` 转发、`Encode` 返回空包，pull 模式返回包（与既有 push/pull 契约一致）。
- **C-015**: 生命周期遵循 `EncoderLifecycle`；`Init()` 前校验 `VideoConfig::IsValid()`；`Flush()` 冲刷剩余输出包；`Release()` 幂等。

## 3. 音频编码契约（MediaCodecAudioEncoder）

- **C-020**: 支持 `AudioCodecType::kAAC`（`audio/mp4a-latm`）；`kOpus` 返回 `kUnsupportedFormat`（v1）。
- **C-021**: `Encode(AudioFrame)` 将 S16 交错 PCM 拷贝入输入缓冲；`pts_us` 按采样时钟推进（每编码帧对应真实时长）。
- **C-022**: 输出为裸 AAC 帧（无 ADTS），`keyframe=false`；`BUFFER_FLAG_CODEC_CONFIG` 缓冲提取的 AudioSpecificConfig 供封装 track 使用（不进入普通输出流）。
- **C-023**: push/pull 与生命周期契约同 C-014/C-015。

## 4. 封装契约（MediaCodecMuxer）

- **C-030**: `SetOutput(ByteSink*)` 须早于首次 `Push`（否则 `kInvalidArgument`）；`nullptr` 解绑。
- **C-031**: 容器懒打开：收到某轨首个 csd（视频=首个含 SPS/PPS 的关键帧，音频=首个含 ASC 的包）才 `addTrack` + `start`；此前的非关键帧数据丢弃（同 FFmpeg 封装先例）。
- **C-032**: 视频轨 `AMediaFormat` 写 `csd-0`/`csd-1` 为**起始码前缀的 SPS/PPS**（`[00 00 00 01]+SPS/PPS`，与 `AMediaCodec_getOutputFormat` 逐字节一致；avcC 盒子或裸 SPS 均被拒/畸形——见研究 R7）；音频轨写 `csd-0`（ASC）；另含 mime/尺寸/fps/采样率/声道元数据。
- **C-032b**: 视频样本以 Annex-B 直传但**剥掉缓冲开头的起始码**（libstagefright 写线程把首个起始码读成空 NAL，导致样本帧错位/崩溃——研究 R7）；csd 之外的 SPS/PPS 随首样本在带内写入。
- **C-033**: 输出时机为 `Finish()`：`AMediaMuxer_stop()` 后从临时文件**一次性回放**全部字节到 `ByteSink::Write` 并 `Flush`；`Finish` 前 ByteSink 不产生字节（与 FFmpeg 分片增量不同——行为差异记录于 C-035）。
- **C-034**: `Finish()`/`Release()` 幂等；`Release()` 未 Finish 时同样落盘回放并关闭临时文件（安全网，同 FFmpeg 析构兜底先例）。
- **C-035**: 行为差异（已知、可接受）：FFmpeg 封装 v1 为分片增量（`fragmented=true` 逐关键帧产出）；Android 封装为停止时一次性产出。两者均产出合法 MP4，媒体内容一致，消费端（ByteSink→文件）结果一致。

## 5. 错误与边界契约

- **C-040**: 所有失败经 `Status`/`Result` 返回，不跨边界抛异常；`kEncodeFailed`（编码器/封装器拒绝）、`kPlatformUnsupported`（无设备能力）、`kUnsupportedFormat`（不支持格式）、`kInvalidArgument`（非法配置/空句柄）。
- **C-041**: 设备缺指定编码器、muxer 停止失败、临时文件 IO 失败等场景均返回明确错误，不产生部分损坏的对外输出。

## 6. 测试契约

- **C-050**: `mediacodec_utils` 纯逻辑在宿主 googletest 覆盖（颜色格式映射、Annex-B 组装、csd 提取、缓冲尺寸、mime 映射）。
- **C-051**: Android 集成验证走交叉编译门禁（`make android-verify` / `bazel build --config android_arm64`）+ 设备/模拟器示例运行（复用 `mediacodec_spike` 验证模式）；Android 上无法运行宿主测试的用例记录为设备验证项。
- **C-052**: 宿主（非 Android）构建与全部既有测试不因本功能回归（FR-010 / SC-004）。
