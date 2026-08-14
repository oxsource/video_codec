# Android verification module (spec 006 MediaCodec backend), three isolation
# steps (research R6/R8):
# - android-build : cross-build the mediacodec_spike (CI gate)
# - android-codec : cross-build the encode_file example only (CI gate)
# - android-raw   : video-only on device (MediaCodec video encoder, no muxer)
# - android-run   : full A/V mux on device (MediaCodec video+audio + MediaMuxer)
# The Android side is MediaCodec-only (no FFmpeg, spec 006 R5); the host FFmpeg
# baseline is covered by `make host_ffmpeg_codec`. Requires ANDROID_NDK_HOME for
# the cross-build and a connected device for the raw/run modes (see
# specs/006-android-mediacodec-backend/quickstart.md).
# All targets are prefixed `android-` so they cannot clash with other modules.
$(call register_module,android)
$(call register_target,android-build)
$(call register_target,android-verify)
$(call register_target,android-codec)
$(call register_target,android-raw)
$(call register_target,android-run)

.PHONY: android-build android-verify android-codec android-raw android-run

android-build: ## Android arm64 cross-build of mediacodec_spike (needs NDK)
	bash $(V)/android_build.sh

android-verify: ## Full android validation (currently = android-build)
	bash $(V)/android_build.sh

android-codec: ## Cross-build the encode_file example for Android (MediaCodec)
	bash $(V)/android_codec.sh build

android-raw: ## Video-only on device: MediaCodec encoder -> raw H.264, no muxer
	bash $(V)/android_codec.sh raw

android-run: ## Full A/V on device: MediaCodec video+audio + MediaMuxer -> MP4
	bash $(V)/android_codec.sh run
