#include "download.hh"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>

#include "ae/log.hh"
#include "ae/sanitize.hh"
#include "../metadata/embedder.hh"
#include "../metadata/extractor.hh"

namespace kb {

namespace fs = std::filesystem;

namespace {

Result<void> check_cancel(const std::atomic<bool> *cancel) {
    if (cancel && cancel->load(std::memory_order_relaxed)) return canceled_error();
    return {};
}

void backoff_sleep(uint32_t attempt) {
    uint64_t delay = DOWNLOAD_RETRY_BASE_DELAY_MS << attempt;
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
}

Result<void> create_dir_all(const std::string &dir) {
    std::error_code ec;
    fs::create_directories(fs::u8path(dir), ec);
    if (ec) return io_error("cannot create directory " + dir + ": " + ec.message());
    return {};
}

std::string two_digit(int value) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d", value);
    return buf;
}

// Cover art fetch (download_io.rs fetch_track_cover / album_download.rs
// download_cover_data): failures are logged and ignored.
std::optional<std::vector<uint8_t>> fetch_cover_bytes(const QobuzApiService &service,
                                                      const std::string &url,
                                                      const std::string &token) {
    auto resp = service.http_client().get(url, {}, {"X-User-Auth-Token: " + token});
    if (!resp.ok() || resp.value().status < 200 || resp.value().status >= 300) {
        AE_LOGD("Cover art download failed for %s", url.c_str());
        return std::nullopt;
    }
    const std::string &body = resp.value().body;
    return std::vector<uint8_t>(body.begin(), body.end());
}

// One download attempt with engine-side Range resume; returns whether the
// server honored the resume (206).
Result<bool> attempt_download(const QobuzApiService &service, int track_id, int format_id,
                              const std::string &path, const DownloadOptions &options) {
    auto offset = detect_partial_file(path);

    auto file_url = service.get_track_file_url(track_id, format_id);
    if (!file_url.ok()) return file_url.error();

    if (!file_url.value().url) {
        return download_error("No download URL for track " + std::to_string(track_id));
    }

    ae::ProgressFn progress;
    if (options.progress) {
        auto fn = options.progress;
        progress = [fn, track_id](uint64_t downloaded, uint64_t total) {
            fn(track_id, downloaded, total);
        };
    }

    auto outcome = service.cdn_client().download_to_file(
        *file_url.value().url, path, offset.value_or(0), progress, options.cancel);
    if (!outcome.ok()) return from_engine(outcome.error());

    bool resumed = offset.has_value() && outcome.value().resumed;
    if (offset && !resumed) {
        AE_LOGW("track %d: server ignored Range request, re-downloaded full file", track_id);
    }
    return resumed;
}

// Runs fn(items[i], i) over at most `concurrency` worker threads.
template <typename Fn>
void run_bounded(size_t count, size_t concurrency, Fn fn) {
    if (count == 0) return;
    std::atomic<size_t> next{0};
    size_t workers_n = std::min(concurrency, count);
    std::vector<std::thread> workers;
    workers.reserve(workers_n);
    for (size_t w = 0; w < workers_n; ++w) {
        workers.emplace_back([&] {
            for (size_t idx; (idx = next.fetch_add(1)) < count;) fn(idx);
        });
    }
    for (auto &t : workers) t.join();
}

// album_download.rs quality_tag
std::string quality_tag(int format_id, const Album &album) {
    if (format_id == quality::MP3_320) return "(320kbps)";

    int ceil_depth = 16;
    double ceil_rate = 44.1;
    if (format_id == quality::FLAC_24_192) {
        ceil_depth = 24;
        ceil_rate = 192.0;
    } else if (format_id == quality::FLAC_24_96) {
        ceil_depth = 24;
        ceil_rate = 96.0;
    }

    int depth = album.maximum_bit_depth ? std::min(*album.maximum_bit_depth, ceil_depth)
                                        : ceil_depth;
    double rate = album.maximum_sampling_rate
                      ? std::min(*album.maximum_sampling_rate, ceil_rate)
                      : ceil_rate;

    std::string rate_str;
    if (rate == static_cast<int>(rate)) {
        rate_str = std::to_string(static_cast<int>(rate));
    } else {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%g", rate);
        rate_str = buf;
    }
    return "(" + std::to_string(depth) + "-" + rate_str + ")";
}

struct AlbumDirectory {
    std::vector<int> track_ids;
    std::string dir;
};

Result<AlbumDirectory> prepare_album_directory(const Album &album, int format_id,
                                               const std::string &output_dir) {
    std::string artist_name =
        (album.artist && album.artist->name) ? *album.artist->name : "Unknown Artist";

    std::string album_display = album.title.value_or("Unknown Album");
    if (album.version && !album.version->empty()) {
        album_display += " (" + *album.version + ")";
    }
    std::string folder_name =
        ae::sanitize_filename(album_display) + " " + quality_tag(format_id, album);

    std::string dir = (fs::u8path(output_dir) / fs::u8path(ae::sanitize_filename(artist_name)) /
                       fs::u8path(folder_name))
                          .u8string();
    auto created = create_dir_all(dir);
    if (!created.ok()) return created.error();

    AlbumDirectory out;
    out.track_ids = album.track_ids.value_or(std::vector<int>{});
    out.dir = std::move(dir);
    return out;
}

std::string album_track_path(const std::string &dir, int track_id, int format_id) {
    std::string filename =
        two_digit(track_id) + "." + Album::extension_for_format(format_id);
    return (fs::u8path(dir) / fs::u8path(filename)).u8string();
}

// Album per-track worker (album_download.rs download_album_tracks task body).
Result<std::string> download_album_track(const QobuzApiService &service, int track_id,
                                         int format_id, const std::string &dir,
                                         const DownloadOptions &options) {
    std::optional<Error> last_err;
    std::string path = album_track_path(dir, track_id, format_id);

    for (uint32_t attempt = 0; attempt <= MAX_DOWNLOAD_RETRIES; ++attempt) {
        auto cancel_check = check_cancel(options.cancel);
        if (!cancel_check.ok()) return cancel_check.error();

        auto offset = detect_partial_file(path);
        auto result = attempt_download(service, track_id, format_id, path, options);

        if (!result.ok()) {
            const Error &e = result.error();
            // 416: the Range offset reached the end — file already complete.
            if (offset && e.code == ErrorCode::Http && e.api_code == 416 &&
                fs::exists(fs::u8path(path))) {
                AE_LOGI("track %d already complete, skipping", track_id);
                return path;
            }
            if (is_retryable_network_error(e) && attempt < MAX_DOWNLOAD_RETRIES) {
                AE_LOGW("track %d download failed (attempt %u): %s", track_id, attempt,
                        e.message.c_str());
                backoff_sleep(attempt);
                last_err = e;
                continue;
            }
            return e;
        }
        AE_LOGI("track %d downloaded (resumed=%d)", track_id, result.value() ? 1 : 0);
        return path;
    }

    return last_err ? *last_err
                    : download_error("Track " + std::to_string(track_id) + " failed after " +
                                     std::to_string(MAX_DOWNLOAD_RETRIES) + " retries");
}

// album_download.rs embed_album_metadata
Result<void> embed_album_metadata(const QobuzApiService &service, const Album &album,
                                  const std::vector<int> &track_ids,
                                  const std::vector<std::string> &paths,
                                  const MetadataConfig &config) {
    std::optional<std::vector<uint8_t>> cover_data;
    if (config.is_enabled(MetadataField::CoverArt) && album.image) {
        if (auto url = best_cover_url(*album.image)) {
            auto token = service.require_auth_token();
            if (!token.ok()) return token.error();
            cover_data = fetch_cover_bytes(service, *url, token.value());
        }
    }

    for (size_t i = 0; i < track_ids.size() && i < paths.size(); ++i) {
        auto track = service.get_track(track_ids[i]);
        if (!track.ok()) return track.error();

        ComprehensiveMetadata meta =
            extract_comprehensive_metadata(track.value(), &album, nullptr);
        meta.cover_art_data = cover_data;

        auto embedded = embed_metadata_in_file(paths[i], meta, config);
        if (!embedded.ok()) return embedded.error();
    }
    return {};
}

} // namespace

std::optional<uint64_t> detect_partial_file(const std::string &path) {
    std::error_code ec;
    auto size = fs::file_size(fs::u8path(path), ec);
    if (ec || size == 0) return std::nullopt;
    return size;
}

Result<std::string> download_track(const QobuzApiService &service, int track_id,
                                   int format_id, const std::string &output_dir,
                                   const DownloadOptions &options) {
    auto cancel_check = check_cancel(options.cancel);
    if (!cancel_check.ok()) return cancel_check.error();

    auto track = service.get_track(track_id);
    if (!track.ok()) return track.error();

    cancel_check = check_cancel(options.cancel);
    if (!cancel_check.ok()) return cancel_check.error();

    const char *ext = Album::extension_for_format(format_id);
    int track_num = track.value().track_number.value_or(track_id);
    std::string title = track.value().title.value_or("Unknown");
    std::string safe_name = ae::sanitize_filename(two_digit(track_num) + ". " + title);
    std::string filename = safe_name + "." + ext;

    auto created = create_dir_all(output_dir);
    if (!created.ok()) return created.error();
    std::string path = (fs::u8path(output_dir) / fs::u8path(filename)).u8string();

    for (uint32_t attempt = 0; attempt <= MAX_DOWNLOAD_RETRIES; ++attempt) {
        cancel_check = check_cancel(options.cancel);
        if (!cancel_check.ok()) return cancel_check.error();

        auto result = attempt_download(service, track_id, format_id, path, options);
        if (result.ok()) break;

        const Error &e = result.error();
        if (is_retryable_network_error(e) && attempt < MAX_DOWNLOAD_RETRIES) {
            AE_LOGW("track %d download failed (attempt %u): %s, retrying with resume",
                    track_id, attempt, e.message.c_str());
            backoff_sleep(attempt);
            continue;
        }
        return e;
    }

    cancel_check = check_cancel(options.cancel);
    if (!cancel_check.ok()) return cancel_check.error();

    AE_LOGI("track %d downloaded to %s", track_id, path.c_str());

    if (options.metadata) {
        auto token = service.require_auth_token();
        if (!token.ok()) return token.error();

        const Album *album_info = track.value().album.get();
        ComprehensiveMetadata meta =
            extract_comprehensive_metadata(track.value(), album_info, nullptr);
        if (meta.cover_art_url) {
            meta.cover_art_data = fetch_cover_bytes(service, *meta.cover_art_url,
                                                    token.value());
        }
        auto embedded = embed_metadata_in_file(path, meta, *options.metadata);
        if (!embedded.ok()) return embedded.error();
    }

    return path;
}

Result<std::vector<std::string>> download_album(const QobuzApiService &service,
                                                const std::string &album_id, int format_id,
                                                const std::string &output_dir,
                                                const DownloadOptions &options) {
    auto cancel_check = check_cancel(options.cancel);
    if (!cancel_check.ok()) return cancel_check.error();

    auto album = service.get_album(album_id, std::optional<std::string>("track_ids"));
    if (!album.ok()) return album.error();

    cancel_check = check_cancel(options.cancel);
    if (!cancel_check.ok()) return cancel_check.error();

    auto prepared = prepare_album_directory(album.value(), format_id, output_dir);
    if (!prepared.ok()) return prepared.error();
    const auto &track_ids = prepared.value().track_ids;
    const std::string &dir = prepared.value().dir;

    size_t concurrency = static_cast<size_t>(options.concurrency.value_or(4));
    std::vector<std::optional<Result<std::string>>> results(track_ids.size());

    run_bounded(track_ids.size(), concurrency, [&](size_t idx) {
        if (options.cancel && options.cancel->load(std::memory_order_relaxed)) return;
        results[idx] = download_album_track(service, track_ids[idx], format_id, dir, options);
    });

    cancel_check = check_cancel(options.cancel);
    if (!cancel_check.ok()) return cancel_check.error();

    std::vector<int> downloaded_ids;
    std::vector<std::string> paths;
    size_t failed = 0;
    for (size_t i = 0; i < results.size(); ++i) {
        if (results[i] && results[i]->ok()) {
            downloaded_ids.push_back(track_ids[i]);
            paths.push_back(results[i]->value());
        } else {
            if (results[i]) {
                AE_LOGE("track %d failed: %s", track_ids[i],
                        results[i]->error().message.c_str());
            }
            ++failed;
        }
    }

    if (paths.empty() && failed > 0) {
        return download_error("All " + std::to_string(failed) + " track(s) failed to download");
    }
    if (failed > 0) {
        AE_LOGW("%zu track(s) failed, %zu succeeded", failed, paths.size());
    }

    AE_LOGI("album %s download complete (%zu tracks)", album_id.c_str(), paths.size());

    if (options.metadata) {
        auto embedded = embed_album_metadata(service, album.value(), downloaded_ids, paths,
                                             *options.metadata);
        if (!embedded.ok()) return embedded.error();
    }

    return paths;
}

Result<std::vector<std::string>> download_playlist(const QobuzApiService &service,
                                                   const std::string &playlist_id,
                                                   int format_id,
                                                   const std::string &output_dir,
                                                   const DownloadOptions &options) {
    auto cancel_check = check_cancel(options.cancel);
    if (!cancel_check.ok()) return cancel_check.error();

    auto playlist = service.get_playlist(playlist_id, std::optional<std::string>("tracks"));
    if (!playlist.ok()) return playlist.error();

    cancel_check = check_cancel(options.cancel);
    if (!cancel_check.ok()) return cancel_check.error();

    std::string title = playlist.value().name.value_or("Unknown Playlist");
    std::string dir =
        (fs::u8path(output_dir) / fs::u8path(ae::sanitize_filename(title))).u8string();
    auto created = create_dir_all(dir);
    if (!created.ok()) return created.error();

    std::vector<int> track_ids;
    if (playlist.value().tracks && playlist.value().tracks->items) {
        for (const auto &t : *playlist.value().tracks->items) {
            if (t && t->id) track_ids.push_back(*t->id);
        }
    }
    if (track_ids.empty()) return download_error("No tracks in playlist");

    size_t concurrency = static_cast<size_t>(options.concurrency.value_or(4));
    std::vector<std::optional<Result<std::string>>> results(track_ids.size());

    run_bounded(track_ids.size(), concurrency, [&](size_t idx) {
        if (options.cancel && options.cancel->load(std::memory_order_relaxed)) return;
        results[idx] = download_track(service, track_ids[idx], format_id, dir, options);
    });

    cancel_check = check_cancel(options.cancel);
    if (!cancel_check.ok()) return cancel_check.error();

    std::vector<std::string> paths;
    size_t failed = 0;
    for (size_t i = 0; i < results.size(); ++i) {
        if (results[i] && results[i]->ok()) {
            paths.push_back(results[i]->value());
        } else {
            if (results[i]) {
                AE_LOGE("track %d failed: %s", track_ids[i],
                        results[i]->error().message.c_str());
            }
            ++failed;
        }
    }

    if (paths.empty() && failed > 0) {
        return download_error("All " + std::to_string(failed) + " track(s) failed to download");
    }
    if (failed > 0) {
        AE_LOGW("%zu track(s) failed, %zu succeeded", failed, paths.size());
    }

    AE_LOGI("playlist %s download complete (%zu tracks)", playlist_id.c_str(), paths.size());
    return paths;
}

Result<std::vector<std::string>> download_artist(const QobuzApiService &service,
                                                 int artist_id, int format_id,
                                                 const std::string &output_dir,
                                                 const DownloadOptions &options) {
    constexpr size_t ALBUM_CONCURRENCY = 2;

    auto cancel_check = check_cancel(options.cancel);
    if (!cancel_check.ok()) return cancel_check.error();

    auto releases = service.get_release_list(artist_id, 50, std::nullopt);
    if (!releases.ok()) return releases.error();

    cancel_check = check_cancel(options.cancel);
    if (!cancel_check.ok()) return cancel_check.error();

    std::vector<std::string> album_ids;
    if (releases.value().items) {
        for (const auto &a : *releases.value().items) {
            if (a && a->id) album_ids.push_back(*a->id);
        }
    }
    if (album_ids.empty()) return download_error("No releases found for artist");

    std::vector<std::optional<Result<std::vector<std::string>>>> results(album_ids.size());

    run_bounded(album_ids.size(), ALBUM_CONCURRENCY, [&](size_t idx) {
        if (options.cancel && options.cancel->load(std::memory_order_relaxed)) return;
        results[idx] =
            download_album(service, album_ids[idx], format_id, output_dir, options);
    });

    cancel_check = check_cancel(options.cancel);
    if (!cancel_check.ok()) return cancel_check.error();

    std::vector<std::string> all_paths;
    size_t failed = 0;
    for (size_t i = 0; i < results.size(); ++i) {
        if (results[i] && results[i]->ok()) {
            const auto &paths = results[i]->value();
            all_paths.insert(all_paths.end(), paths.begin(), paths.end());
        } else {
            if (results[i]) {
                AE_LOGE("album %s failed: %s", album_ids[i].c_str(),
                        results[i]->error().message.c_str());
            }
            ++failed;
        }
    }

    if (all_paths.empty() && failed > 0) {
        return download_error("All " + std::to_string(failed) + " album(s) failed to download");
    }
    if (failed > 0) {
        AE_LOGW("%zu album(s) failed, %zu tracks succeeded", failed, all_paths.size());
    }

    AE_LOGI("artist %d download complete (%zu tracks)", artist_id, all_paths.size());
    return all_paths;
}

} // namespace kb
