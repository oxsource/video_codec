# video_codec build/verify mechanism — top-level entry point.
#
# Modules live in mk/ and are self-describing, AOSP-style: each file calls
#   $(call register_module, <name>)      # unique module name (like LOCAL_MODULE)
#   $(call register_target,  <target>)   # targets it owns (prefixed <module>-<action>)
# (see mk/rules.mk). Duplicate module/target/alias names abort the build.
#
# Canonical targets are namespaced per module (e.g. host-verify, android-build) so
# modules can never clash; friendly short aliases live in mk/aliases.mk.
#
# To add a new category, create mk/<name>.mk — it is picked up automatically.

SHELL := /bin/bash
V := scripts/verify

# Core registry must be loaded before the modules it validates; exclude it from the
# module wildcard so it is not double-included (which would reset the registry).
include mk/rules.mk
include $(filter-out mk/rules.mk,$(wildcard mk/*.mk))

# Running a bare `make` shows the target list.
.DEFAULT_GOAL := help
