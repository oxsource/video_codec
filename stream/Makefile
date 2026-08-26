# video_stream build/verify mechanism — top-level entry point.
#
# Modules live in mk/ and are self-describing, AOSP-style. See mk/rules.mk.

SHELL := /bin/bash
V := scripts/verify

include mk/rules.mk
include $(filter-out mk/rules.mk,$(wildcard mk/*.mk))

.DEFAULT_GOAL := help