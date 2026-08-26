$(call register_module, help)

$(call register_target, help)
help:
	@echo "video_stream — available targets:"
	@echo ""
	@echo "  build-stream     Build stream library (api + core)"
	@echo "  build-all        Alias for build-stream"
	@echo "  build-webrtc     Build WebRTC/WHIP backend"
	@echo "  build-example    Build encode_and_push example"
	@echo "  build-android    Android arm64 cross-build (core + mock, needs NDK)"
	@echo "  test-stream      Run stream tests (pending tests/ suite, T053)"
	@echo "  clean            Remove generated out/ artifacts"
	@echo "  verify           Full host validation"
	@echo "  help             Show this message"
