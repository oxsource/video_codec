# WebRTC/WHIP 推流模块优化任务清单

涉及文件：
- `stream/src/backend/webrtc/webrtc_backend.cc`（及对应 `.h`）
- `stream/src/backend/webrtc/whip_session.cc`（及对应 `.h`）

背景问题：mediamtx 8892 端口（非 WebRTC 通路）能正常看到画面，8889 端口（WHEP 观看）报 `peer connection closed`。怀疑与 WHIP 信令层 URL 处理、连接状态误判、H264 码流格式相关。

---

## 第一批：必须改（P0，建议按此顺序实施）

### P0-1. WHIP Location Header 相对路径解析（whip_session.cc）
> 审核结论（2026-09-04）：✅ 可接受，安排整改 —— 已核对 `whip_session.cc:163-169` 原样保存 Location，`PatchIce/Delete`（`:178/:206`）若拿到相对路径会直接请求失败；解析逻辑建议与 P0-8 一并落地。

**问题**：`Create()` 中直接把 `Location` header 原样存入 `resource_url_`，未处理相对路径（如 mediamtx 常返回 `/whip/xxx/whipsession/yyy`）。后续 `PatchIce()`/`Delete()` 用这个半吊子 URL 发请求会失败，可能导致 mediamtx 主动关闭会话。

**要求**：
- 新增 `ResolveLocation(base_url, location)` 工具函数：若 `location` 已是绝对 URL（`http://`/`https://` 开头）直接返回；否则从 `whip_endpoint` 中提取 `scheme://host[:port]`，与相对路径拼接成完整 URL。
- `Create()` 中调用该函数生成 `resource_url_`，并打印解析后的完整 URL。
- 若响应中缺失 `Location` header，视为异常：记录 warning 日志并通过 `on_error_` 回调上报，不要静默跳过。

### P0-2. WHIP 响应状态码与内容严格校验（whip_session.cc）
> 审核结论（2026-09-04）：✅ 可接受，安排整改 —— 已确认 `Response::ok()` 表示任意 2xx（`response.h:47`），现仅检查 `ok()+body 非空`（`whip_session.cc:153`）；补齐 `201` + SDP `v=0`（先 trim 再判断）+ Content-Type 存在性校验即可。

**问题**：`Create()` 只判断 `response.ok()`（可能对任意 2xx 都返回 true）和 `body().empty()`，未校验：
1. 状态码必须是 `201`（WHIP 规范要求）；
2. body 是否为合法 SDP（可能是服务端出错时返回的 JSON/HTML 错误页）。

若把非 SDP 内容传给 `pc_->setRemoteDescription()`，会导致 PeerConnection 进入错误状态后被关闭——这是当前 "peer connection closed" 的重点怀疑对象之一。

**要求**：
- 显式判断 `response.status() != 201` 时报错并调用 `on_error_`，日志中打印实际状态码。
- 校验 body 中包含 `v=0`（SDP 起始行）等基本特征，不合法则报错，不继续往下走。
- 建议同时校验响应 `Content-Type` 是否为 `application/sdp`（若服务端有返回该字段）。

### P0-3. Connect() 状态变量完整 reset（webrtc_backend.cc）
> 审核结论（2026-09-04）：✅ 可接受，安排整改 —— 已核对 `Connect()`（`webrtc_backend.cc:85`）入口未重置任何标志位，且重连复用同一后端实例（`stream_impl.cc:202`），残留状态风险真实存在。

**问题**：`Connect()` 开头未重置 `offer_ready_`、`gathering_complete_`、`answer_ready_`、`connected_state_`、`connect_result_`、`current_offer_`、`session_id_`。重连场景下旧状态残留会导致新一轮流程被跳过或提前误判成功。

**要求**：
- `Connect()` 函数最开始，在锁内将上述所有标志位重置为初始值（`connect_result_` 重置为 `kOk`）。
- `current_offer_`、`session_id_` 等字符串成员一并 clear。

### P0-4. wait_for 必须检查超时返回值（webrtc_backend.cc）
> 审核结论（2026-09-04）：✅ 可接受，安排整改（最严重，优先实施）—— 已确认两处 `wait_for` 均忽略返回值（`webrtc_backend.cc:241/:251`），超时后仍会落到 `return connect_result_`（默认 `kOk`, `:256`），`StreamImpl::Start()` 见 `kOk` 即置 `kStreaming`（`stream_impl.cc:90-91`），导致"实际未建连却误报成功"。需在 `video::codec::Status` 新增 `kTimeout`；建议同步修正错误映射：当前 WHIP 网络/HTTP 错误一律记为 `kEncodeFailed` 语义不符，宜改用网络类状态码。

**问题**：两处 `cv_.wait_for(lock, 10s, predicate)` 均未检查返回值。无论是否真正等到条件成立，函数都会继续往下执行并可能返回 `kOk`，导致上层误以为推流已建立。

**要求**：
- 每处 `wait_for` 的布尔返回值必须显式接收并判断。
- 超时（返回 false）时记录 error 日志（区分是"等 WHIP 应答超时"还是"等 ICE 连接超时"），并返回明确的超时状态码（若 `video::codec::Status` 无对应值，需新增，如 `kTimeout`）。
- 不允许超时后仍走到 `return connect_result_`（此时 `connect_result_` 可能仍是默认的 `kOk`）。

### P0-5. WHIP 创建流程合并为单一路径（webrtc_backend.cc）
> 审核结论（2026-09-04）：✅ 可接受，安排整改 —— 已核对 `:136-166` 与 `:188-217` 存在两套重复的"注册回调+Create"逻辑，且 `offer_ready_` 存在无锁读写（`:115/:174/:210`）；重构时务必保留现有语义：gathering 完成路径须用 `pc_->localDescription()` 取含完整 candidate 的最终 offer 发送。

**问题**：`onLocalDescription` 与 `onGatheringStateChange` 中各自重复了一份"注册 `SetOnReady`/`SetOnError` + 调用 `whip_session_->Create()`"的逻辑，存在维护风险和竞态可能（两个回调线程都可能判断 `offer_ready_ == false` 后同时执行）。

**要求**：
- 抽出统一的私有方法，如 `SendOfferToWhip(const std::string& whip_endpoint, const std::string& offer)`，内部完成注册回调 + 调用 `Create()`。
- 两处回调统一在锁内做"检查并置位 `offer_ready_`"的原子判断，只有成功置位的一方才调用 `SendOfferToWhip`，避免竞态：
  ```cpp
  bool should_send;
  {
    std::lock_guard<std::mutex> lock(mtx_);
    should_send = !offer_ready_;
    if (should_send) offer_ready_ = true;
  }
  if (should_send) SendOfferToWhip(whip_endpoint, offer);
  ```

### P0-6. 校验 packet.data 是否为 Annex-B 格式（webrtc_backend.cc）
> 审核结论（2026-09-04）：⚠️ 调整后接受，**降级为 P1 诊断项，不安排 P0 整改** —— 源码核对调用链：FFmpeg 编码器已通过 `h264_mp4toannexb` bitstream filter（`ffmpeg_video.cc:95-117,191-204`），MediaCodec 后端按 Annex-B 组装（`mediacodec_video.cc`），即**当前上游到达 `SendVideo()` 的数据已是 Annex-B 起始码格式**，`Separator::StartSequence`（`webrtc_backend.cc:230-231`）配置正确，并非"看不到画面"的根因。实施上仅保留一次性/按需的格式探测日志即可，**不要加转换逻辑**（会破坏现有 Annex-B 流）。若未来接入输出 AVCC 的上游，libdatachannel 的 `H264RtpPacketizer` 同样支持 `Separator::Length`（AVCC 4 字节长度前缀），届时再做选择。

**问题**：`H264RtpPacketizer` 构造时指定 `Separator::StartSequence`，要求输入必须是 Annex-B 格式（NAL 前带 `00 00 00 01`/`00 00 01` 起始码）。若编码器实际输出 AVCC 格式（4 字节长度前缀，无起始码），打包会产生错误的 NAL 边界，是"能连上但画面异常/看不到画面"的高优先级怀疑点。

**要求**：
- 在 `SendVideo()` 中增加一次性或按需的格式检测日志：读取 `packet.data` 前 4 字节，判断是否匹配 Annex-B 起始码模式。
- 若确认上游是 AVCC 格式，需在 `SendVideo()` 前增加转换逻辑（将长度前缀替换为起始码），或推动编码层直接输出 Annex-B。

### P0-7. 增加 video_track_->onOpen() / onClosed() 回调（webrtc_backend.cc）
> 审核结论（2026-09-04）：✅ 可接受，安排整改 —— `Track` 继承 `Channel`，`onOpen/onClosed` 接口存在（libdatachannel `channel.hpp`）。实现时注意：send-only media track 的 open 时序需在联调中实测（open 通常由对端回传 RTCP/媒体触发），`SendVideo()` 在 open 前可先返回轻量错误码而非阻塞，避免头几帧被静默门控。

**问题**：当前逻辑中 PeerConnection 进入 `Connected` 状态即认为可以发送数据（`connected_state_ = true`），但 PC Connected 不等于 Track 的 DTLS/SRTP 通道已打开。过早调用 `send()` 可能被静默丢弃（无异常抛出，`try/catch` 抓不到）。

**要求**：
- `addTrack` 后立即注册 `onOpen`/`onClosed` 回调，用独立的 `track_open_` 标志维护状态并通过 `cv_` 通知。
- `SendVideo()` 增加前置检查：track 未 open 时返回 `kNotInitialized`（或专门的状态码），不要静默尝试发送。

### P0-8. 增加 WHIP DELETE 会话清理（webrtc_backend.cc + whip_session.cc）
> 审核结论（2026-09-04）：✅ 可接受，安排整改 —— 已核对 `Disconnect()`（`webrtc_backend.cc:300-311`）未调用 `whip_session_` 任何清理接口；`Delete()` 当前函数体已优先使用 `resource_url_`（`whip_session.cc:206`），签名中 `whip_endpoint/session_id` 确属冗余，简化为无参数版本可行。

**问题**：`WebrtcBackend::Disconnect()` 当前完全没有调用 `whip_session_` 的任何清理接口，mediamtx 端 session 只能靠自身超时机制回收。反复重连场景下可能导致新旧 session 冲突，表现为连接被异常关闭。

**要求**：
- `WhipSession::Delete()` 改为无参数版本，内部直接使用已保存的 `resource_url_`（当前签名要求外部传 `whip_endpoint`/`session_id`，但函数体已经优先使用 `resource_url_`，属于冗余参数，一并简化）。
- `resource_url_` 为空时记录 warning 并直接返回 false，不要用 `whip_endpoint + "/" + session_id` 做无意义的猜测拼接。
- `WebrtcBackend::Disconnect()` 中在关闭 PeerConnection 之前调用 `whip_session_->Delete()`，并记录调用结果日志。

### P0-9. 增加 PeerConnection / Track / WHIP 全链路详细日志
> 审核结论（2026-09-04）：✅ 可接受，安排整改。

**要求**（贯穿以上所有改动）：
- ICE candidate 收集阶段打印每个 candidate 的类型（host/srflx/relay），用于判断 STUN 是否生效、NAT 穿透情况。
- WHIP `Create()` 中打印完整请求 URL、响应状态码、`Location` 原始值与解析后的 `resource_url_`。
- 关键状态迁移（`offer_ready_`、`gathering_complete_`、`answer_ready_`、`connected_state_`、`track_open_`）变化时打印前后值，便于用日志还原完整时序。

---

## 第二批：建议改（P1）

### P1-1. 状态标志位线程安全
> 审核结论（2026-09-04）：✅ 可接受，安排整改 —— 已确认存在无锁跨线程读写：`connected_`（`webrtc_backend.cc:64` 写 vs `:255/:294` 读）、`gathering_complete_`（`:174`）、`offer_ready_`（`:115/:210`）。改原子后注意与现有互斥锁"二选一"管理同一变量，避免双写歧义；需 `cv_` 阻塞等待的场景保留 notify。

`offer_ready_`、`gathering_complete_`、`answer_ready_`、`connected_state_`、`connected_`、`track_open_` 存在跨线程（libdatachannel 内部线程 vs 调用线程）读写但部分在锁外访问的情况，建议统一改为 `std::atomic<bool>`，需要阻塞等待的场景仍保留条件变量 `notify`。

### P1-2. 检查 video_track_->send() 返回值
> 审核结论（2026-09-04）：✅ 可接受，安排整改 —— 已确认 `Channel::send()` 返回 false 表示背压缓冲（libdatachannel `channel.hpp`），非致命；`StreamStats` 新增 `packets_dropped` 并在返回 false 时计数 + warning 日志合理。

`SendVideo()` 中 `video_track_->send(msg)` 未检查返回值。`send()` 返回 false 通常代表发送缓冲区背压，不一定是致命错误，建议：
- 在 `StreamStats` 中新增 `packets_dropped` 字段；
- 返回 false 时计数并记录 warning 日志，而非当作正常发送成功处理。

### P1-3. 完善 RTCP Sender Report
> 审核结论（2026-09-04）：✅ 可接受，安排整改 —— 头文件已证实 `RtcpSrReporter` 无内部定时器，仅暴露 `setNeedsToReport()`（需外部周期调用后由后续 outgoing 消息携带 SR）；当前代码从未触发，故 **SR 确实不会周期性发送**。需补周期驱动（如约 1s 触发一次）。

确认当前 libdatachannel 版本下 `RtcpSrReporter` 是否需要外部定时驱动才能按周期发送 SR 包（部分版本依赖 `rtpConfig->timestamp` 更新自动触发，部分需要显式调用）。如需要，补充定时器逻辑，否则接收端 A/V 同步、丢包统计可能不准。

### P1-4. Connected / Streaming 状态拆分
> 审核结论（2026-09-04）：✅ 可接受，安排整改 —— 已核对重连/启动判定不依赖 `kStreaming` 上报（`stream_impl.cc:197-213` 仅以 `kDisconnected` 触发重连），拆分安全。注意 `stream_impl.cc:198` 会用后端 `StreamStatus` 整体覆盖上层 `status_`，新增状态值需在上层状态机中同步认知。

当前 `StreamState` 只有 `kConnecting`/`kStreaming`/`kDisconnected`/`kDestroyed`，`kStreaming` 在 PC `Connected` 时就置位，与实际"是否已能正常发送数据"脱节。建议拆分：
- `kIceConnected`：PC 进入 Connected，但 track 尚未 open；
- `kStreaming`：track open 且已成功发送过至少一帧。
上层业务回调应以 `kStreaming` 作为"确认在推流"的判断依据。

### P1-5. rtc::InitLogger 避免重复初始化
> 审核结论（2026-09-04）：✅ 可接受，安排整改 —— 已核对构造每次调用 `rtc::InitLogger`（`webrtc_backend.cc:48`），`std::call_once` 包裹合理。

`WebrtcBackend` 构造函数中每次都调用 `rtc::InitLogger(...)`，多实例场景下重复初始化全局 logger。建议用 `std::call_once` 包裹。

### P1-6. SendAudio() 去除虚假统计
> 审核结论（2026-09-04）：⚠️ 调整后接受，安排整改 —— 假统计已确认（`webrtc_backend.cc:293-298` 仅递增计数）。调整点：`video::codec::Status` 枚举**没有 `kNotImplemented`**，应返回 `kUnsupportedOperation`；且上层 `StreamImpl::SendAudio`（`stream_impl.cc:158-166`）对任意非 `kOk` 都会递增 `frames_dropped` 并刷新 `last_error`，逐帧返回会导致日志刷屏，建议仅在首帧置一次 `last_error` 并停用统计递增。

当前 `SendAudio()` 没有真正的音频 track 和发送逻辑，只是在 `connected_` 为 true 时递增 `packets_sent`/`bytes_sent`。若暂不支持音频，应直接返回 `kNotImplemented`，不要伪造发送成功的统计数据，避免掩盖"无声音"问题的排查线索。

### P1-7. PatchIce() 同步修复相对 URL 问题
> 审核结论（2026-09-04）：✅ 可接受（随 P0-1 一并处理）—— 确认当前走 Vanilla ICE 未调用 `PatchIce`；相对 URL 解析工具函数与 P0-1 复用即可，无需单独排期。

当前未被调用（走的是 Vanilla ICE，非 Trickle ICE），可暂不处理；若后续启用 Trickle ICE 优化建连延迟，需要同 P0-1 一样处理 `resource_url_` 的相对路径解析，避免遗留同样的 bug。

---

## 验证方式建议
1. 修复 P0-4（超时判断）后，人为制造网络异常（如临时防火墙拦截 WHIP 端口），确认 `Connect()` 能返回超时状态而非误报 `kOk`。
2. 修复 P0-1/P0-2 后，打印完整的 `Location` 原始值与解析结果，对照 mediamtx 服务端日志确认 PATCH/DELETE 请求是否命中正确 URL。
3. 修复 P0-6 后，抓取实际 `packet.data` 前若干字节，确认起始码格式，必要时联调编码层输出格式。
4. 修复 P0-8 后，反复执行"连接-断开-重连"多轮，观察 mediamtx 端是否还有残留 session 或异常关闭现象。
5. 全部完成后，用 WHEP 播放端（8889）与现有 8892 通路做对比测试，确认画面表现一致。
