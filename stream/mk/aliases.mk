# Friendly short aliases for the canonical prefixed targets (conflict-free).
$(call register_module,aliases)
$(call register_alias,build,host-build)
$(call register_alias,verify,host-verify)
$(call register_alias,build-android,android-build)
$(call register_alias,verify-android,android-verify)
$(call register_alias,clean,clean-out)
