# Friendly short aliases for the canonical prefixed targets (conflict-free, unique).
# Each alias maps to a <module>-<action> target; duplicates abort the build.
$(call register_module,aliases)
$(call register_alias,verify,host-verify)
$(call register_alias,build,host-build)
$(call register_alias,spike,host-spike)
$(call register_alias,build-android,android-build)
$(call register_alias,verify-android,android-verify)
$(call register_alias,docs,docs-check)
