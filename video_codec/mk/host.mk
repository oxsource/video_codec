# Host verification module. Runs on the dev host (macOS ARM64 / Linux x86_64).
# Targets are prefixed `host-`.
$(call register_module,host)
$(call register_target,host-build)
$(call register_target,host-spike)
$(call register_target,host_ffmpeg_codec)
$(call register_target,host-encode-push)
$(call register_target,host-verify)

.PHONY: host-build host-spike host_ffmpeg_codec host-encode-push host-verify

host-build: ## Host build: bazel build //... (all targets)
	bash $(V)/host_build.sh $(BAZEL_OPTS)

host-spike: ## Run the FFmpeg libx264 encode spike only (no full rebuild)
	bash $(V)/host_spike.sh

host_ffmpeg_codec: ## Run the encode_file demo (SMPTE color bars -> .h264) + ffprobe assert
	bash $(V)/host_ffmpeg_codec.sh

host-encode-push: ## Run the stream encode_and_push example (default 30s; pass DURATION=N to override)
	bash $(V)/stream_encode_push.sh $(DURATION)

host-verify: ## Full host validation: build + stream example + codec verification
	bash $(V)/host_verify.sh $(BAZEL_OPTS)
