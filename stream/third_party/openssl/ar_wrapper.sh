#!/usr/bin/env bash
# Archiver shim for the rules_foreign_cc OpenSSL build.
#
# Bazel's macOS cc_toolchain drives `libtool` with ARFLAGS="-static -s" and the
# output archive passed positionally (`libtool -static -s <out.a> <objs...>`),
# but Apple's libtool requires `-o`. OpenSSL's Makefile uses exactly that
# positional form, so translate it into a working libtool invocation.
#
# On Linux the GNU `ar` flags pass through unchanged.
set -euo pipefail

if [[ "${1:-}" == "-static" ]]; then
    exec /usr/bin/libtool -static -o "$3" "${@:4}"
fi

exec /usr/bin/ar "$@"
