#pragma once

// Port of src/api/service.rs, src/api/auth.rs and the content/favorites
// endpoints (src/api/content/*.rs, src/api/favorites.rs). Synchronous: each
// call blocks on the underlying curl request.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ae/http.hh"
#include "../core/errors.hh"
#include "../core/models.hh"
#include "requests.hh"

namespace kb {

class QobuzApiService {
public:
    struct Config {
        std::string app_id;
        std::string app_secret;
        // PEM CA bundle path for TLS verification (required on Android).
        std::string ca_bundle_path;
        // Optional .env path used by create() and refresh_app_credentials()
        // for credential persistence. Empty disables persistence.
        std::string env_path;
    };

    // with_credentials: explicit app credentials (errors when empty).
    static Result<QobuzApiService> with_credentials(Config config);

    // new(): credentials from env_path's .env, else extracted from the
    // Qobuz web player (and saved back to env_path when set).
    static Result<QobuzApiService> create(Config config);

    // --- Authentication (src/api/auth.rs)

    // Email + password (MD5-hashed before sending). Returns (token, user_id).
    Result<std::pair<std::string, int64_t>> login(const std::string &email,
                                                  const std::string &password);

    // user_id + existing token. Returns the account country code ("US", ...).
    Result<std::string> login_with_token(const std::string &user_id,
                                         const std::string &auth_token);

    // Credential lookup in priority order: QOBUZ_USER_ID+QOBUZ_USER_AUTH_TOKEN,
    // then QOBUZ_EMAIL/QOBUZ_USERNAME+QOBUZ_PASSWORD. `get_env` returns the
    // value or nullopt (replaces process env vars, which Android lacks).
    Result<void> authenticate_with_getter(
        const std::function<std::optional<std::string>(const char *)> &get_env);

    // Re-extracts app credentials from the web player; once per session.
    Result<void> refresh_app_credentials();

    // --- Search (src/api/content)

    Result<SearchResult> search_catalog(const std::string &query,
                                        std::optional<int> limit = {},
                                        std::optional<int> offset = {}) const;
    Result<ItemSearchResult<std::shared_ptr<Album>>> search_albums(
        const std::string &query, std::optional<int> limit = {},
        std::optional<int> offset = {}) const;
    Result<ItemSearchResult<std::shared_ptr<Artist>>> search_artists(
        const std::string &query, std::optional<int> limit = {},
        std::optional<int> offset = {}) const;
    Result<ItemSearchResult<std::shared_ptr<Track>>> search_tracks(
        const std::string &query, std::optional<int> limit = {},
        std::optional<int> offset = {}) const;
    Result<ItemSearchResult<std::shared_ptr<Playlist>>> search_playlists(
        const std::string &query, std::optional<int> limit = {},
        std::optional<int> offset = {}) const;

    // --- Browse

    Result<Album> get_album(const std::string &album_id,
                            const std::optional<std::string> &extra = {}) const;
    Result<Artist> get_artist(int artist_id,
                              const std::optional<std::string> &extra = {}) const;
    Result<Track> get_track(int track_id) const;
    Result<Playlist> get_playlist(const std::string &playlist_id,
                                  const std::optional<std::string> &extra = {}) const;
    Result<ItemSearchResult<std::shared_ptr<Album>>> get_release_list(
        int artist_id, std::optional<int> limit = {}, std::optional<int> offset = {}) const;

    // --- Downloads (src/api/content/tracks.rs)

    Result<FileUrl> get_track_file_url(std::int64_t track_id, int format_id) const;

    // --- Favorites (src/api/favorites.rs)

    Result<void> add_user_favorites(const std::vector<int> &item_ids,
                                    const std::string &item_type) const;
    Result<void> delete_user_favorites(const std::vector<int> &item_ids,
                                       const std::string &item_type) const;
    Result<UserFavorites> get_user_favorites(const std::string &item_type,
                                             std::optional<int> limit = {},
                                             std::optional<int> offset = {}) const;
    Result<UserFavorites> get_user_favorite_ids() const;

    // --- State

    const std::string &base_url() const { return base_url_; }
    const std::string &app_id() const { return app_id_; }
    const std::string &app_secret() const { return app_secret_; }
    Result<std::string> require_auth_token() const;
    void set_auth_token(std::string token) { user_auth_token_ = std::move(token); }
    bool is_authenticated() const { return user_auth_token_.has_value(); }

    // API client (x-app-id header, browser UA).
    const ae::HttpClient &http_client() const { return *api_client_; }
    // Minimal CDN client (no API headers, avoids confusing CDN edges).
    const ae::HttpClient &cdn_client() const { return *cdn_client_; }

    api::RequestAuth request_auth(const std::string &token) const {
        return api::RequestAuth{app_id_, app_secret_, token};
    }

private:
    QobuzApiService() = default;

    static Result<QobuzApiService> build_service(Config config);
    void rebuild_http_client();

    // Helpers mirroring content/mod.rs.
    Result<std::string> do_signed_get_raw(const std::string &endpoint,
                                          api::Params params) const;
    template <typename T>
    Result<T> do_signed_get(const std::string &endpoint, api::Params params) const {
        auto raw = do_signed_get_raw(endpoint, std::move(params));
        if (!raw.ok()) return raw.error();
        return parse_json<T>(raw.value());
    }
    template <typename T>
    Result<T> search(const std::string &endpoint, const std::string &query,
                     std::optional<int> limit, std::optional<int> offset) const;
    template <typename T, typename Id>
    Result<T> get_by_id(const std::string &endpoint, const char *id_field, const Id &id,
                        const std::optional<std::string> &extra) const;
    Result<void> modify_favorites(const std::vector<int> &item_ids,
                                  const std::string &item_type,
                                  const std::string &endpoint) const;
    Result<UserFavorites> fetch_user_favorites(api::Params params) const;

    std::string base_url_;
    std::string app_id_;
    std::string app_secret_;
    std::string ca_bundle_path_;
    std::string env_path_;
    std::optional<std::string> user_auth_token_;
    // shared_ptr so the service stays cheaply movable/copy-safe for worker
    // threads; ae::HttpClient itself is thread-safe per request.
    std::shared_ptr<ae::HttpClient> api_client_;
    std::shared_ptr<ae::HttpClient> cdn_client_;
    bool credentials_refreshed_ = false;
};

} // namespace kb
