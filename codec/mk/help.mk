# Help module: lists every `## `-annotated target across all modules.
# -h suppresses the per-file `name:` prefix that grep adds when given multiple files.
$(call register_module,help)
$(call register_target,help)
$(call register_target,modules)

.PHONY: help modules

help: ## List all targets
	@grep -hE '^[a-zA-Z_-]+:.*?## ' $(MAKEFILE_LIST) \
	  | awk 'BEGIN {FS = ":.*?## "}; {printf "  \033[36m%-16s\033[0m %s\n", $$1, $$2}'

modules: ## List registered modules (AOSP LOCAL_MODULE equivalents)
	@echo "modules: $(REGISTERED_MODULES)"
