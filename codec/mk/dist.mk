# Distribution module: publishes the SDK (public headers + shared library) to
# dist/<platform>/ (gitignored). Usage:
#   make dist-publish                      # host
#   make dist-publish PLATFORM=android_arm64
$(call register_module,dist)
$(call register_target,dist-publish)

.PHONY: dist-publish

dist-publish: ## Publish headers + shared library to dist/<platform>/ (cf. scripts/publish.sh)
	bash scripts/publish.sh $(PLATFORM)
