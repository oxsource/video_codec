load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:git.bzl", "new_git_repository")

def _curl():
    http_archive(
        name = "curl",
        urls = ["https://github.com/curl/curl/releases/download/curl-8_9_0/curl-8.9.0.tar.gz"],
        sha256 = "14d931fa98a329310dca7b190d047c3d4987674b1f466481f5490e4e12067ba4",
        strip_prefix = "curl-8.9.0",
        build_file = "//third_party/curl:BUILD.bazel",
    )

def _googletest():
    http_archive(
        name = "com_google_googletest",
        sha256 = "8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7",
        strip_prefix = "googletest-1.14.0",
        urls = ["https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz"],
    )

def _bazel_skylib():
    http_archive(
        name = "bazel_skylib",
        urls = ["https://github.com/bazelbuild/bazel-skylib/archive/refs/tags/1.6.1.tar.gz"],
        sha256 = "aede1b60709ac12b3461ee0bb3fa097b58a86fbfdb88ef7e9f90424a69043167",
        strip_prefix = "bazel-skylib-1.6.1",
    )

def _ffmpeg():
    http_archive(
        name = "ffmpeg",
        urls = ["https://ffmpeg.org/releases/ffmpeg-6.1.tar.xz"],
        sha256 = "488c76e57dd9b3bee901f71d5c95eaf1db4a5a31fe46a28654e837144207c270",
        strip_prefix = "ffmpeg-6.1",
        build_file = "@video_codec//third_party/ffmpeg:BUILD.bazel",
    )

def _openssl():
    # OpenSSL 3.0 LTS, built from source via rules_foreign_cc (configure_make)
    # so it is cross-platform (host macOS/Linux + Android arm64), mirroring the
    # codec module's source-build philosophy. build_file wraps libssl/libcrypto.
    http_archive(
        name = "openssl",
        urls = ["https://github.com/openssl/openssl/releases/download/openssl-3.0.15/openssl-3.0.15.tar.gz"],
        sha256 = "23c666d0edf20f14249b3d8f0368acaee9ab585b09e1de82107c66e1f3ec9533",
        strip_prefix = "openssl-3.0.15",
        build_file = "//third_party/openssl:BUILD.bazel",
    )

def _rules_foreign_cc():
    http_archive(
        name = "rules_foreign_cc",
        sha256 = "5816f4198184a1e0e682d7e6b817331219929401e2f18358fac7f7b172737976",
        strip_prefix = "rules_foreign_cc-0.10.0",
        url = "https://github.com/bazelbuild/rules_foreign_cc/archive/refs/tags/0.10.0.tar.gz",
    )

def _libyuv():
    http_archive(
        name = "libyuv",
        sha256 = "119e44ae87da1f1362e7d005fcaaf36176f34df43df5e01619fcfec175490e26",
        strip_prefix = "libyuv-79d22698bc2f11f6b0b014141fe94b82aa5a53b3",
        urls = ["https://github.com/lemenkov/libyuv/archive/79d22698bc2f11f6b0b014141fe94b82aa5a53b3.tar.gz"],
        build_file = "@video_codec//third_party/libyuv:BUILD.bazel",
    )

def _libdatachannel():
    new_git_repository(
        name = "libdatachannel",
        remote = "https://github.com/paullouisageneau/libdatachannel.git",
        tag = "v0.21.2",
        init_submodules = True,
        build_file = "//third_party/libdatachannel:BUILD.bazel",
    )

def video_stream_setup():
    if not native.existing_rule("bazel_skylib"):
        _bazel_skylib()
    if not native.existing_rule("curl"):
        _curl()
    if not native.existing_rule("com_google_googletest"):
        _googletest()
    if not native.existing_rule("ffmpeg"):
        _ffmpeg()
    if not native.existing_rule("openssl"):
        _openssl()
    if not native.existing_rule("rules_foreign_cc"):
        _rules_foreign_cc()
    if not native.existing_rule("libyuv"):
        _libyuv()
    if not native.existing_rule("libdatachannel"):
        _libdatachannel()