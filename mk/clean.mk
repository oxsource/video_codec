# Clean module: removes generated test-output artifacts.
$(call register_module,clean)
$(call register_target,clean-out)

.PHONY: clean-out

clean-out: ## Remove generated test-output files under out/ (gitignored)
	rm -rf out/* out/.[!.]* 2>/dev/null || true
	@echo "[clean] out/ contents removed"
