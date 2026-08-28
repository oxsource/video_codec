workspace(name = "video_codec")

load("//:deps.bzl", "video_codec_setup")
load("@bazel_tools//tools/cpp:cc_configure.bzl", "cc_configure")

video_codec_setup()

# cpp_network's transitive deps (curl, openssl) are declared here with
# @cpp_network// labels instead of calling cpp_network_setup(), because the
# pinned commit uses bare // labels that only resolve within the cpp_network
# workspace itself.

load("//:deps.bzl", "cpp_network_deps")

cpp_network_deps()

cc_configure()

load("@rules_foreign_cc//foreign_cc:repositories.bzl", "rules_foreign_cc_dependencies")

rules_foreign_cc_dependencies(
    register_built_tools = False,
    register_built_pkgconfig_toolchain = False,
)

# ---- Android cross-build ------------------------------------------------
load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

http_archive(
    name = "rules_android_ndk",
    sha256 = "65aedff0cd728bee394f6fb8e65ba39c4c5efb11b29b766356922d4a74c623f5",
    strip_prefix = "rules_android_ndk-0.1.2",
    urls = ["https://github.com/bazelbuild/rules_android_ndk/releases/download/v0.1.2/rules_android_ndk-v0.1.2.tar.gz"],
)

load("@rules_android_ndk//:rules.bzl", "android_ndk_repository")

android_ndk_repository(name = "androidndk")
