$(call register_module, test)

$(call register_target, test-stream)
test-stream:
	bazel test //tests/...

$(call register_target, test-quick)
test-quick:
	bazel test //tests:stream_interface_test //tests:backend_selection_test