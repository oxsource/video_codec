# Stream test module. Tests are pending (T053).
$(call register_module,test)

$(call register_target, test-stream)
test-stream: ## Run stream tests (pending tests/ suite, T053)
	@echo "[test] stream/tests/ suite not committed yet (T053) — nothing to run"

$(call register_target, test-quick)
test-quick: ## Run stream tests (pending tests/ suite, T053)
	@echo "[test] stream/tests/ suite not committed yet (T053) — nothing to run"
