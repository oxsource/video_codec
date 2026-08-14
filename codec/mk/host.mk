# Host verification module (scaffold Phase 5 / US1 FFmpeg encode spike).
# Targets are prefixed `host-`. Runs on the dev host (macOS ARM64 / Linux x86_64).
$(call register_module,host)
$(call register_target,host-build)
$(call register_target,host-spike)
$(call register_target,host_ffmpeg_codec)
$(call register_target,host-verify)

.PHONY: host-build host-spike host_ffmpeg_codec host-verify

host-build: ## Host build: bazel build //... (compiles FFmpeg 6.1 from source)
	bash $(V)/host_build.sh

host-spike: ## Run the FFmpeg libx264 encode spike only (no full rebuild)
	bash $(V)/host_spike.sh

host_ffmpeg_codec: ## Run the encode_file demo (SMPTE color bars -> .h264) + ffprobe assert
	bash $(V)/host_ffmpeg_codec.sh

host-verify: ## Full host validation: build + spike + encode_file example + ffprobe asserts
	bash $(V)/host_verify.sh
