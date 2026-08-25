$(call register_module, help)

$(call register_target, help)
help:
	@echo "video_stream — available targets:"
	@echo ""
	@echo "  build-stream    Build stream library"
	@echo "  build-server    Build WHIP test server"
	@echo "  test-stream     Run all tests"
	@echo "  test-quick      Run core unit tests only"
	@echo "  help            Show this message"