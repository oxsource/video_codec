# Clean module: removes generated test-output artifacts.
# `out/` is the shared gitignored test-output dir written by the host/android
# verify scripts (example MP4s, spike streams). All targets prefixed `clean-`.
$(call register_module,clean)
$(call register_target,clean-out)

.PHONY: clean-out

clean-out: ## Remove generated test-output files under out/ (gitignored)
	rm -rf out/* out/.[!.]* 2>/dev/null || true
	@echo "[clean] out/ contents removed"
