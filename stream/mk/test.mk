$(call register_module, test)

# NOTE: the stream/tests/ suite is not committed yet (tasks.md T053 follow-up),
# so there are no test targets to run. Once the suite lands, replace the body
# with `bazel test //tests/...`.
$(call register_target, test-stream)
test-stream:
	@echo "[test] stream/tests/ suite not committed yet (T053) — nothing to run"

$(call register_target, test-quick)
test-quick:
	@echo "[test] stream/tests/ suite not committed yet (T053) — nothing to run"
