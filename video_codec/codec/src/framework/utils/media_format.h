// media_format.h
#pragma once

#include <string_view>

namespace video {
namespace codec {
namespace utils {

// Central registry of encoded-media file extensions so callers (examples,
// muxer selection, upload routing, ...) never hand-code suffix literals like
// ".mp4". Static-class utility: constants + one matching helper, no state.
class MediaFormat {
 public:
  // Container formats
  static constexpr std::string_view kMp4 = ".mp4";  // MPEG-4 container
  static constexpr std::string_view kMp3 = ".mp3";  // MPEG audio layer III
  static constexpr std::string_view kM4a = ".m4a";  // AAC audio in MP4
  static constexpr std::string_view kMov = ".mov";  // QuickTime container
  static constexpr std::string_view kTs = ".ts";    // MPEG transport stream
  static constexpr std::string_view kMkv = ".mkv";  // Matroska container

  // Elementary streams (raw Annex-B / ADTS, not containerized)
  static constexpr std::string_view kH264 = ".h264";
  static constexpr std::string_view kHevc = ".hevc";
  static constexpr std::string_view kAac = ".aac";
  static constexpr std::string_view kOpus = ".opus";

  // True if `path` ends with `ext`, ignoring ASCII case (".MP4" matches kMp4).
  static bool HasExtension(std::string_view path, std::string_view ext);

 private:
  MediaFormat() = delete;  // static-class: no instances

  static char Lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
  }
};

inline bool MediaFormat::HasExtension(std::string_view path, std::string_view ext) {
  if (ext.empty() || path.size() < ext.size()) return false;
  const std::string_view suffix = path.substr(path.size() - ext.size());
  for (size_t i = 0; i < ext.size(); ++i) {
    if (Lower(suffix[i]) != Lower(ext[i])) return false;
  }
  return true;
}

}  // namespace utils
}  // namespace codec
}  // namespace video
