$(call register_module, build)

$(call register_target, build-stream)
build-stream:
	bazel build //src/api:stream_api //src/core:stream_core $(BAZEL_OPTS)

$(call register_target, build-all)
build-all: build-stream

$(call register_target, build-webrtc)
build-webrtc:
	bazel build //src/backend/webrtc:webrtc_backend $(BAZEL_OPTS)

$(call register_target, build-example)
build-example:
	bazel build //src/examples:encode_and_push $(BAZEL_OPTS)