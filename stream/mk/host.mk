# Host verification module (codec parity). Runs on the dev host
# (macOS ARM64 / Linux x86_64). Targets are prefixed `host-`.
$(call register_module,host)
$(call register_target,host-build)
$(call register_target,host-verify)

.PHONY: host-build host-verify

host-build: ## Host build: bazel build //... (compiles all targets)
	bash $(V)/host_build.sh $(BAZEL_OPTS)

host-verify: ## Full host validation: build stream library + example
	bash $(V)/host_verify.sh $(BAZEL_OPTS)
