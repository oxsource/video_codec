# Android verification module. Cross-builds the platform-independent stream core
# + mock backend and the codec mediacodec_spike (no openssl/libdatachannel/curl,
# which are host-only for now). Requires ANDROID_NDK_HOME.
# Targets are prefixed `android-`.
$(call register_module,android)
$(call register_target,android-build)
$(call register_target,android-verify)
$(call register_target,android-codec)
$(call register_target,android-raw)
$(call register_target,android-run)
$(call register_target,android-surface)

.PHONY: android-build android-verify android-codec android-raw android-run android-surface

android-build: ## Android arm64 cross-build of stream core + mock + codec spike (needs NDK)
	bash $(V)/android_build.sh $(BAZEL_OPTS)

android-verify: ## Full android validation (currently = android-build)
	bash $(V)/android_build.sh $(BAZEL_OPTS)

android-codec: ## Cross-build the encode_file example for Android (MediaCodec)
	bash $(V)/android_codec.sh build

android-raw: ## Video-only on device: MediaCodec encoder -> raw H.264, no muxer
	bash $(V)/android_codec.sh raw

android-run: ## Full A/V on device: MediaCodec video+audio + MediaMuxer -> MP4
	bash $(V)/android_codec.sh run

android-surface: ## Full A/V on device via hardware input surface (zero-copy)
	bash $(V)/android_codec.sh surface
