# video_codec build/verify mechanism — top-level entry point.
#
# Modules live in mk/ and are self-describing, AOSP-style: each file calls
#   $(call register_module, <name>)      # unique module name (like LOCAL_MODULE)
#   $(call register_target,  <target>)   # targets it owns (prefixed <module>-<action>)
# (see mk/rules.mk). Duplicate module/target/alias names abort the build.
#
# Canonical targets are namespaced per module (e.g. host-verify, android-build) so
# modules can never clash; friendly short aliases live in mk/aliases.mk.

SHELL := /bin/bash
V := scripts/verify

include mk/rules.mk
include $(filter-out mk/rules.mk,$(wildcard mk/*.mk))

.DEFAULT_GOAL := help
