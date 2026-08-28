# Stream build module.
$(call register_module,build)

$(call register_target, build-stream)
build-stream: ## Build stream library (api + core)
	bazel build //stream/src/api:stream_api //stream/src/core:stream_core $(BAZEL_OPTS)

$(call register_target, build-all)
build-all: build-stream

$(call register_target, build-webrtc)
build-webrtc: ## Build WebRTC/WHIP backend
	bazel build //stream/src/backend/webrtc:webrtc_backend $(BAZEL_OPTS)

$(call register_target, build-example)
build-example: ## Build encode_and_push example
	bazel build //stream/src/examples:encode_and_push $(BAZEL_OPTS)
