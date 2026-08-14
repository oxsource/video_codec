# Research: Input Surface（Android MediaCodec 输入面）

**Branch**: `007-input-surface-api` | **Date**: 2026-08-14 | **Spec**: [spec.md](spec.md)

> 澄清结论（spec）：v1 仅 Android MediaCodec `createInputSurface` 硬件面；非安卓返回 `nullptr`；
> `CreateInputSurface()` 直接返回 `void*`（`ANativeWindow*`），删除 `InputSurface` 类与 `input_surface.h`。

## R1: 输入面 API 形态与时间戳机制

- **Decision**: `VideoEncoder::CreateInputSurface()` 返回类型改为 `void*`（Android 为 `ANativeWindow*`），默认 `nullptr`；删除 `InputSurface` 类与 `api/input_surface.h`（spec FR-010、用户澄清）。surface 模式在 `VideoConfig` 以 `input_surface` 标志声明（因 `AMediaCodec_configure` 需在 start 前使用 `COLOR_FormatSurface`，无法事后切换）。每帧时间戳由调用者经句柄设定：CPU 绘制路径用 `ANativeWindow_setBuffersTimestamp`（API 26+，单位纳秒）。
- **Rationale**: 用户明确"直接返回 void*、更简洁灵活"；`COLOR_FormatSurface`(0x7F000789) 是硬件编码器输入面的必需配置（参考项目 `android_media.cc` 确认）。时间戳为 A/V 同步所需（spec FR-004/SC-002）。
- **关键缺口（NDK 25.2）**: `ANativeWindow_setBuffersTimestamp` **不在** `native_window.h` 且 **不在** NDK `libandroid.so` 导出符号中；`AMediaCodec_setInputSurfaceTimestamp`（API 30）**不在** `NdkMediaCodec.h` 且 **不在** `libmediandk.so` 导出符号中（均经 `llvm-nm -D` 验证）。二者均为平台公开 API（API 26+/30+，target API 31 设备可用）。
- **适配方案**: 以 `dlsym(RTLD_DEFAULT, "ANativeWindow_setBuffersTimestamp")` 运行时解析（规避 NDK 25.2 头文件/库未导出，属工具链版本缺口，非设备相关）；解析失败时退化为无显式时间戳（记录限制）。示例（`encode_file --surface`）用该方式设定每帧时间戳。
- **Alternatives**:
  - EGL 路径 `eglPresentationTimeANDROID`（参考项目 `external_image_demo.cc` 用 `eglSwapBuffers` + 渲染上下文）→ 需 EGL/GLES 依赖与 GPU，v1 示例选 CPU 绘制（更简单、无 GPU 依赖）；两者对框架同等有效（框架只给句柄）。
  - 升级 NDK 以获得头文件 → 工具链升级超出本功能范围，且 dlsym 方案已足够。

## R2: CPU 绘制路径与缓冲格式

- **Decision**: 示例/验证用 CPU 绘制：`ANativeWindow_setBuffersGeometry(window, w, h, WINDOW_FORMAT_RGBA_8888)` → `ANativeWindow_lock`（得 `ANativeWindow_Buffer`，RGBA_8888）→ 填像素 → `ANativeWindow_setBuffersTimestamp`(ns)（dlsym）→ `ANativeWindow_unlockAndPost`。硬件编码器内部完成 RGB→YUV 转换。
- **Rationale**: `ANativeWindow_lock` 为 CPU 可绘制路径（头文件与 `libandroid.so` 符号已确认），RGBA_8888 是最通用软件绘制格式；SMPTE 彩条可直接按 RGBA 生成（与既有 I420 生成器分离）。
- **契约**: 帧缓冲在 lock 与 unlockAndPost 之间归调用者；提交后（unlockAndPost）缓冲归系统/编码器所有，调用者不得再访问（spec FR-004）。格式/尺寸/stride 在 lock 的 `ANativeWindow_Buffer` 中暴露（spec FR-003）。

## R3: EOS / Flush 语义（安卓通用）

- **Decision**: 输入面路径下 `Flush()` 调用 `AMediaCodec_signalEndOfInputStream`（API 26，NDK 头文件/符号确认）触发 EOS，然后 drain 输出；drain 循环在 EOS 缓冲到达**或 deadline 超时**时结束（默认 5s）。
- **Rationale**: surface 模式无 input buffer，EOS 只能经 `signalEndOfInputStream`，这是 Android 输入面路径的**标准通用语义**。但各厂商硬件编码器对 `signalEndOfInputStream` 后的 EOS 输出缓冲行为**不统一**——部分编码器（含 Amlogic 实测设备）从不发出 EOS 输出缓冲，所有帧已在之前的 drain 中产出；若框架死等 EOS，会在这类设备上永久阻塞。因此 **drain 必须带 deadline 兜底，这是对全部 Android 设备的通用健壮性要求**，而非某厂商特例。
- **Alternatives**: 无 deadline 死等 EOS → 否决（存在不发 EOS 的厂商编码器，会挂死）；仅依赖 EOS 缓冲 → 否决（行为跨设备不保证）。

## R4: 输入模式互斥

- **Decision**: 同一 `MediaCodecVideoEncoder` 实例上，"CPU 帧输入（`Encode(VideoFrame)`）"与"输入面输入"互斥：`VideoConfig.input_surface = true`（surface 模式）时，`Encode(VideoFrame)` 返回 `kUnsupportedOperation`；CPU 模式下 `CreateInputSurface()` 返回 `nullptr`。
- **Rationale**: MediaCodec 语义硬约束（Android 通用）——`createInputSurface` 后 input buffers 不可用（`queueInputBuffer` 失败）；互斥在配置期即定（`input_surface` 标志），不依赖运行时检测。
- **Alternatives**: 运行时探测混用并拒绝 → 配置期声明更简单且避免非法状态（否决运行时方案）。

## R5: 示例与验证策略

- **Decision**: `encode_file` 增加 `--surface` 模式（`__ANDROID__` 守卫，非安卓构建忽略该模式）：创建 surface 编码器 → `CreateInputSurface()` 取窗口 → CPU 绘制 RGBA SMPTE 60 帧（含时间戳）→ `Flush()` → 产出 `clip-android.mp4`。`android_codec.sh` 增加 `surface` 模式：push/run/pull + ffprobe 双流 + ffmpeg 解码校验（复用既有 run 校验逻辑）。
- **Rationale**: 与既有示例/验证工具同构（spec 006 R8 逐步隔离模式）；设备实测是 surface 路径唯一权威验证（宿主无法运行）；复用既有 ffprobe/解码校验脚本。
- **宿主单测**: `mediacodec_utils` 新增 surface 相关纯逻辑（如格式常量、VideoConfig surface 校验）可宿主覆盖；编码器互斥/句柄逻辑属设备验证项（C-0xx）。

## R6: 构建接线

- **Decision**: `backend/android` 链接 `libmediandk`（既有 `third_party/android_ndk:android_media_codec`）+ `libandroid`（`ANativeWindow_*` 符号所在）；示例 `encode_file` 在 Android 构建链接 `-landroid`（示例调用 `ANativeWindow_lock` 等）。`dlsym` 不需要额外库（libdl 隐含）。
- **Rationale**: 已用 `llvm-nm -D` 确认 `libandroid.so` 导出 `ANativeWindow_lock`/`setBuffersGeometry`/`unlockAndPost`/`acquire`（API 31）；`libmediandk.so` 导出 `createInputSurface`/`signalEndOfInputStream`。框架自身只调用 mediandk（`createInputSurface`），`ANativeWindow_*` 由示例调用 → 示例链接 landroid。
- **Alternatives**: 框架内部也管理 ANativeWindow 引用（acquire/release）→ v1 由调用者负责句柄生命周期（句柄有效至编码器 `Release()`，spec FR-007），框架不 acquire。

## R7: 输入面背压与输出泵送（Poll）

- **Decision**: surface 模式下调用者每绘制一帧后须调用 `VideoEncoder::Poll()`（新增最小方法）排空已就绪输出；硬件编码器在输出队列未排空时停止消费输入面缓冲，导致输入面队列填满、`eglSwapBuffers`/`ANativeWindow_lock` 永久阻塞（实测）。
- **实证**: 设备（Amlogic be11，Android 12）实测——仅绘制+`Flush`（Flush 时才 drain）时，绘制循环在第 2 帧 `eglSwapBuffers` 永久阻塞（编码器无输出消费）；每帧 `Poll()` 后 60 帧正常编码（含 5s deadline 兜底）产出合法 MP4。参考项目 `android_media.cc` 的 `Poll()` 同因（每帧 `SwapBuffers` 后 drain）。
- **Rationale**: 这是硬件编码器通用的背压语义（非设备特例）；`Poll()` 是 surface 流的最小输出泵送接口，CPU 模式为 no-op。
- **Alternatives**: 内部后台线程周期 drain → 引入线程模型，否决；仅 Flush 时 drain → 输入面背压卡死，否决。
