# Android verification module (codec parity). Cross-builds the platform-
# independent stream core + mock backend (no openssl/libdatachannel/curl, which
# are host-only for now). Requires ANDROID_NDK_HOME for the cross-build.
# Targets are prefixed `android-`.
$(call register_module,android)
$(call register_target,android-build)
$(call register_target,android-verify)

.PHONY: android-build android-verify

android-build: ## Android arm64 cross-build of stream_core + mock_backend (needs NDK)
	bash $(V)/android_build.sh $(BAZEL_OPTS)

android-verify: ## Full android validation (currently = android-build)
	bash $(V)/android_build.sh $(BAZEL_OPTS)
