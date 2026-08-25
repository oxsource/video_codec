$(call register_module, build)

$(call register_target, build-stream)
build-stream:
	bazel build //src/api:stream_api //src/core:stream_core

$(call register_target, build-all)
build-all: build-stream

$(call register_target, build-server)
build-server:
	bazel build //src/test_server:whip_test_server