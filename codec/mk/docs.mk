# Docs verification module (Phase 5 T026). Quick consistency check of the
# produced scaffold against the docs — no build required.
$(call register_module,docs)
$(call register_target,docs-check)

.PHONY: docs-check

docs-check: ## Quick check: docs match produced layout (no build)
	bash $(V)/docs_check.sh
