#!/usr/bin/env bash
# Publish the video_codec SDK (public headers + shared library) to dist/<platform>/.
#
# Usage:
#   ./scripts/publish.sh                  # host (FFmpeg backend)
#   ./scripts/publish.sh android_arm64    # cross-build for Android arm64 (MediaCodec)
#
# Layout (atlas-style, with versioned library symlinks + pkg-config):
#   dist/<platform>/include/video_codec/  public headers (self-contained flat set)
#   dist/<platform>/lib/                  libvideo_codec.so[.VERSION] (+ .dylib on macOS)
#   dist/<platform>/lib/pkgconfig/video_codec.pc
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

PLATFORM="${1:-host}"
BAZEL_CONFIG=""
PLATFORM_DIR="host"
if [[ -n "${PLATFORM}" && "${PLATFORM}" != "host" ]]; then
    BAZEL_CONFIG="--config=${PLATFORM}"
    PLATFORM_DIR="$(echo "${PLATFORM}" | tr '_' '-')"
fi

PREFIX="${ROOT}/dist/${PLATFORM_DIR}"
echo "=== video_codec SDK publish (${PLATFORM}) -> ${PREFIX} ==="

bazel build //codec/src/framework/public:video_codec_shared ${BAZEL_CONFIG} --config=shared

rm -rf "${PREFIX}"
mkdir -p "${PREFIX}/include/video_codec" "${PREFIX}/lib/pkgconfig"

# Public headers: the umbrella + every framework header it pulls in.
for h in \
    codec/src/framework/public/include/video_codec/video_codec.h \
    codec/src/framework/public/include/video_codec/video_codec_export.h \
    codec/src/framework/core/export.h \
    codec/src/framework/core/types.h \
    codec/src/framework/core/status.h \
    codec/src/framework/core/result.h \
    codec/src/framework/core/log_slot.h \
    codec/src/framework/core/packet_sink.h \
    codec/src/framework/api/audio_encoder.h \
    codec/src/framework/api/video_encoder.h \
    codec/src/framework/api/codec_factory.h \
    codec/src/framework/api/input_surface.h \
    codec/src/framework/api/muxer.h \
    codec/src/framework/io/byte_sink.h \
    codec/src/framework/io/file_byte_sink.h \
    codec/src/framework/utils/media_format.h; do
    cp "${h}" "${PREFIX}/include/video_codec/"
done

# Shared library with versioned symlinks.
VERSION="1.0.0"
SO_VERSION="${VERSION%%.*}"

is_macos() {
    [[ "${PLATFORM}" == macos_* ]] || { [[ "${PLATFORM}" == "host" ]] && [[ "$(uname -s)" == "Darwin" ]]; }
}

if is_macos; then
    EXT=".dylib"
    cp "bazel-bin/codec/src/framework/public/libvideo_codec_shared${EXT}" "${PREFIX}/lib/libvideo_codec.${VERSION}${EXT}"
    ln -sf "libvideo_codec.${VERSION}${EXT}" "${PREFIX}/lib/libvideo_codec.${SO_VERSION}${EXT}"
    ln -sf "libvideo_codec.${SO_VERSION}${EXT}" "${PREFIX}/lib/libvideo_codec${EXT}"
    install_name_tool -id "@rpath/libvideo_codec.${SO_VERSION}${EXT}" \
        "${PREFIX}/lib/libvideo_codec.${VERSION}${EXT}" 2>/dev/null || true
else
    EXT=".so"
    cp "bazel-bin/codec/src/framework/public/libvideo_codec_shared${EXT}" "${PREFIX}/lib/libvideo_codec${EXT}.${VERSION}"
    ln -sf "libvideo_codec${EXT}.${VERSION}" "${PREFIX}/lib/libvideo_codec${EXT}.${SO_VERSION}"
    ln -sf "libvideo_codec${EXT}.${SO_VERSION}" "${PREFIX}/lib/libvideo_codec${EXT}"
fi

# pkg-config
sed "s|@PREFIX@|${PREFIX}|g" "${ROOT}/scripts/video_codec.pc.in" > "${PREFIX}/lib/pkgconfig/video_codec.pc"

echo "=== SDK ready: ${PREFIX} ==="
echo "  include/video_codec/  $(ls "${PREFIX}/include/video_codec/" | wc -l | tr -d ' ') headers"
echo "  lib/                  libvideo_codec${EXT}"
