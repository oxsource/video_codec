load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")

def _ffmpeg():
    # Pin FFmpeg 6.1 release; build_file wraps libavcodec/libavutil as cc_library.
    # TODO(phase2): replace <pinned-sha256> with the real ffmpeg-6.1 tarball sha256
    # before the first `bazel build` (fetch will fail on a placeholder hash).
    http_archive(
        name = "ffmpeg",
        urls = ["https://ffmpeg.org/releases/ffmpeg-6.1.tar.xz"],
        sha256 = "<pinned-sha256>",
        strip_prefix = "ffmpeg-6.1",
        build_file = "//third_party/ffmpeg:BUILD.bazel",
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

def video_codec_setup():
    if not native.existing_rule("bazel_skylib"):
        _bazel_skylib()
    if not native.existing_rule("ffmpeg"):
        _ffmpeg()
    if not native.existing_rule("com_google_googletest"):
        _googletest()
