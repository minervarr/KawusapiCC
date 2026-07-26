#pragma once

// Port of src/api/content/{download_io,tracks,album_download,
// playlist_download,artist_download}.rs and service_download.rs.
// Synchronous, with bounded worker-thread concurrency replacing the tokio
// semaphore + spawn pattern.

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "../api/service.hh"
#include "../core/errors.hh"
#include "../metadata/config.hh"

namespace kb {

inline constexpr uint32_t MAX_DOWNLOAD_RETRIES = 3;
inline constexpr uint64_t DOWNLOAD_RETRY_BASE_DELAY_MS = 2000;

// Absolute per-track progress; total is 0 when unknown.
using TrackProgressFn =
    std::function<void(int track_id, uint64_t downloaded, uint64_t total)>;

struct DownloadOptions {
    // Metadata embedding configuration; disabled when unset.
    std::optional<MetadataConfig> metadata;
    // Concurrent track downloads for album/playlist (default 4).
    std::optional<int> concurrency;
    // Checked between and during downloads.
    const std::atomic<bool> *cancel = nullptr;
    TrackProgressFn progress;
};

// `Some(size)` when a non-empty partial file exists at path.
std::optional<uint64_t> detect_partial_file(const std::string &path);

// Downloads one track to `output_dir` as "NN. Title.ext" with Range-resume
// and retry; embeds metadata when configured. Returns the file path.
Result<std::string> download_track(const QobuzApiService &service, int track_id,
                                   int format_id, const std::string &output_dir,
                                   const DownloadOptions &options = {});

// Downloads a whole album into "{output_dir}/{artist}/{album (quality)}/",
// tracks named "ID.ext", with bounded concurrency. One track failing does
// not abort the rest; fails only when every track failed.
Result<std::vector<std::string>> download_album(const QobuzApiService &service,
                                                const std::string &album_id, int format_id,
                                                const std::string &output_dir,
                                                const DownloadOptions &options = {});

// Downloads playlist tracks into "{output_dir}/{playlist name}/".
Result<std::vector<std::string>> download_playlist(const QobuzApiService &service,
                                                   const std::string &playlist_id,
                                                   int format_id,
                                                   const std::string &output_dir,
                                                   const DownloadOptions &options = {});

// Downloads every album of an artist (first 50 releases), two albums at a
// time, each with the configured per-album track concurrency.
Result<std::vector<std::string>> download_artist(const QobuzApiService &service,
                                                 int artist_id, int format_id,
                                                 const std::string &output_dir,
                                                 const DownloadOptions &options = {});

} // namespace kb
