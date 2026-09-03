load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")
load("@bazel_tools//tools/build_defs/repo:git.bzl", "new_git_repository")

# ---- Shared dependencies (deduplicated from video_codec_deps + video_stream_deps)

def _bazel_skylib():
    http_archive(
        name = "bazel_skylib",
        urls = ["https://github.com/bazelbuild/bazel-skylib/archive/refs/tags/1.6.1.tar.gz"],
        sha256 = "aede1b60709ac12b3461ee0bb3fa097b58a86fbfdb88ef7e9f90424a69043167",
        strip_prefix = "bazel-skylib-1.6.1",
    )

def _googletest():
    http_archive(
        name = "com_google_googletest",
        sha256 = "8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7",
        strip_prefix = "googletest-1.14.0",
        urls = ["https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz"],
    )

def _ffmpeg():
    http_archive(
        name = "ffmpeg",
        urls = ["https://ffmpeg.org/releases/ffmpeg-6.1.tar.xz"],
        sha256 = "488c76e57dd9b3bee901f71d5c95eaf1db4a5a31fe46a28654e837144207c270",
        strip_prefix = "ffmpeg-6.1",
        build_file = "@video_codec//codec/third_party/ffmpeg:BUILD.bazel",
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
        build_file = "@video_codec//codec/third_party/libyuv:BUILD.bazel",
    )

# ---- Stream-only dependencies

def _nlohmann_json():
    http_archive(
        name = "nlohmann_json",
        sha256 = "a22461d13119ac5c78f205d3df1db13403e58ce1bb1794edc9313677313f4a9d",
        urls = ["https://github.com/nlohmann/json/releases/download/v3.11.3/include.zip"],
        build_file = "@video_codec//stream/third_party/nlohmann_json:BUILD.bazel",
    )

def _libdatachannel():
    new_git_repository(
        name = "libdatachannel",
        remote = "https://github.com/paullouisageneau/libdatachannel.git",
        tag = "v0.21.2",
        init_submodules = True,
        build_file = "@video_codec//stream/third_party/libdatachannel:BUILD.bazel",
    )

# ---- cpp_network: the only external dependency

def _cpp_network_http():
    http_archive(
        name = "cpp_network",
        sha256 = "5ab53545da2ca87853d9fe8b4cf506c15f0b805b4d7f9941fcdf17c57a4afd29",
        strip_prefix = "cpp_network-v1.0.2/cpp_network",
        urls = ["https://github.com/oxsource/cpp_network/releases/download/v1.0.2/cpp_network-v1.0.2.tar.gz"],
        build_file = "@video_codec//stream/third_party/cpp_network:BUILD.bazel",
    )

def _cpp_network_local():
    # The local cpp_network repo nests its actual workspace under
    # cpp_network/cpp_network (WORKSPACE + BUILD.bazel + cpp_network_deps.bzl).
    # Point local_repository there so the repo's own root BUILD.bazel provides
    # the @cpp_network package (local_repository has no build_file attr).
    native.local_repository(
        name = "cpp_network",
        path = "../../cpp_network/cpp_network",
    )

# ---- Setup function

def video_codec_setup():
    if not native.existing_rule("bazel_skylib"):
        _bazel_skylib()
    if not native.existing_rule("com_google_googletest"):
        _googletest()
    if not native.existing_rule("ffmpeg"):
        _ffmpeg()
    if not native.existing_rule("rules_foreign_cc"):
        _rules_foreign_cc()
    if not native.existing_rule("libyuv"):
        _libyuv()
    if not native.existing_rule("nlohmann_json"):
        _nlohmann_json()
    if not native.existing_rule("libdatachannel"):
        _libdatachannel()
    if not native.existing_rule("cpp_network"):
        # _cpp_network_local()
        _cpp_network_http()
