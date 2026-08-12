# AOSP-inspired module registry (mk/*.mk).
#
# Each module file declares its identity and owns its targets, mirroring AOSP's
# LOCAL_MODULE convention:
#
#   $(call register_module, <name>)      # unique module name (like LOCAL_MODULE)
#   $(call register_target,  <target>)   # every target the module defines
#   $(call register_alias,   <alias>, <target>)  # friendly short name (mk/aliases.mk)
#
# Duplicate module names, duplicate target names, or duplicate aliases ABORT the
# build instead of silently overriding (GNU make would otherwise let the last
# definition win). Canonical targets are prefixed <module>-<action> so modules can
# never clash.
#
# NOTE: macro bodies must be single-line (no backslash continuations) — line
# continuations inside `define` break `$(eval ...)` persistence across included files.

REGISTERED_MODULES :=
TARGET_OWNER :=
REGISTERED_ALIASES :=

define register_module
$(if $(filter $1,$(REGISTERED_MODULES)),$(error [mk] duplicate module '$1' — every mk/*.mk must register a unique module name (AOSP LOCAL_MODULE)),$(eval REGISTERED_MODULES += $1))
endef

define register_target
$(if $(filter $1,$(TARGET_OWNER)),$(error [mk] duplicate target '$1' — already owned by another module; prefix your targets with <module>- to avoid clashes),$(eval TARGET_OWNER += $1))
endef

define register_alias
$(if $(filter $1,$(REGISTERED_ALIASES)),$(error [mk] duplicate alias '$1' — every friendly alias must be unique),$(eval REGISTERED_ALIASES += $1)$(eval $1: $2)$(eval .PHONY: $1))
endef
