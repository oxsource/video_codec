REGISTERED_MODULES :=
TARGET_OWNER :=
REGISTERED_ALIASES :=

define register_module
$(if $(filter $1,$(REGISTERED_MODULES)),$(error [mk] duplicate module '$1' — every mk/*.mk must register a unique module name),$(eval REGISTERED_MODULES += $1))
endef

define register_target
$(if $(filter $1,$(TARGET_OWNER)),$(error [mk] duplicate target '$1' — already owned by another module; prefix your targets with <module>- to avoid clashes),$(eval TARGET_OWNER += $1))
endef

define register_alias
$(if $(filter $1,$(REGISTERED_ALIASES)),$(error [mk] duplicate alias '$1' — every friendly alias must be unique),$(eval REGISTERED_ALIASES += $1)$(eval $1: $2)$(eval .PHONY: $1))
endef