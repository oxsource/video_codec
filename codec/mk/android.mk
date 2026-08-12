# Android verification module (US2 MediaCodec cross-build spike).
# Device-run is not wired up; this cross-builds the android-only spike target.
# Requires the deferred `android_ndk_repository(name = "androidndk")` registration
# in WORKSPACE before it will link (see tasks.md T023 / T022 notes).
# All targets are prefixed `android-` so they cannot clash with other modules.
$(call register_module,android)
$(call register_target,android-build)
$(call register_target,android-verify)

.PHONY: android-build android-verify

android-build: ## Android arm64 cross-build of mediacodec_spike (needs NDK)
	bash $(V)/android_build.sh

android-verify: ## Full android validation (currently = android-build)
	bash $(V)/android_build.sh
