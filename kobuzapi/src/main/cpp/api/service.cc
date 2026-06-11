#include "service.hh"

#include <future>
#include <type_traits>

#include "ae/hash.hh"
#include "ae/log.hh"
#include "../core/credentials.hh"
#include "../core/json.hh"

namespace kb {

namespace {

constexpr const char *BASE_URL = "https://www.qobuz.com/api.json/0.2";
constexpr const char *BROWSER_UA =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:109.0) Gecko/20100101 Firefox/110.0";

ae::HttpClient::Options api_client_options(const std::string &app_id,
                                           const std::string &ca_bundle_path) {
    ae::HttpClient::Options opts;
    opts.user_agent = BROWSER_UA;
    opts.default_headers = {"x-app-id: " + app_id};
    opts.ca_bundle_path = ca_bundle_path;
    opts.connect_timeout_ms = 10000;
    return opts;
}

ae::HttpClient::Options cdn_client_options(const std::string &ca_bundle_path) {
    ae::HttpClient::Options opts;
    opts.ca_bundle_path = ca_bundle_path;
    opts.connect_timeout_ms = 10000;
    return opts;
}

// Subset of the login response the auth flow needs.
struct LoginResponse {
    std::optional<std::string> user_auth_token;
    std::optional<int64_t> user_id;
    std::optional<std::string> country_code;
};

void from_json(const nlohmann::json &j, LoginResponse &v) {
    jsonu::get_opt(j, "user_auth_token", v.user_auth_token);
    if (jsonu::present(j, "user")) {
        const auto &user = j.at("user");
        jsonu::get_opt(user, "id", v.user_id);
        jsonu::get_opt(user, "country_code", v.country_code);
    }
}

void push_pagination_params(api::Params &params, std::optional<int> limit,
                            std::optional<int> offset) {
    if (limit) params.emplace_back("limit", std::to_string(*limit));
    if (offset) params.emplace_back("offset", std::to_string(*offset));
}

std::string trim_copy(const std::string &s) {
    size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

} // namespace

Result<QobuzApiService> QobuzApiService::build_service(Config config) {
    if (config.app_id.empty() || config.app_secret.empty()) {
        return initialization_error("app_id and app_secret must be non-empty");
    }

    QobuzApiService service;
    service.base_url_ = BASE_URL;
    service.app_id_ = std::move(config.app_id);
    service.app_secret_ = std::move(config.app_secret);
    service.ca_bundle_path_ = std::move(config.ca_bundle_path);
    service.env_path_ = std::move(config.env_path);
    service.rebuild_http_client();
    return service;
}

void QobuzApiService::rebuild_http_client() {
    api_client_ = std::make_shared<ae::HttpClient>(
        api_client_options(app_id_, ca_bundle_path_));
    if (!cdn_client_) {
        cdn_client_ = std::make_shared<ae::HttpClient>(cdn_client_options(ca_bundle_path_));
    }
}

Result<QobuzApiService> QobuzApiService::with_credentials(Config config) {
    return build_service(std::move(config));
}

Result<QobuzApiService> QobuzApiService::create(Config config) {
    if (config.app_id.empty() || config.app_secret.empty()) {
        std::optional<std::pair<std::string, std::string>> creds;
        if (!config.env_path.empty()) {
            auto loaded = load_app_credentials(config.env_path);
            if (!loaded.ok()) return loaded.error();
            creds = loaded.take();
        }

        if (!creds) {
            AE_LOGI("No .env credentials found, extracting from web player");
            ae::HttpClient::Options opts;
            opts.user_agent = "Mozilla/5.0";
            opts.ca_bundle_path = config.ca_bundle_path;
            ae::HttpClient plain(opts);

            auto extracted = extract_from_web_player(plain);
            if (!extracted.ok()) return extracted.error();
            creds = extracted.take();

            if (!config.env_path.empty()) {
                auto saved = save_app_credentials(config.env_path, creds->first,
                                                  creds->second);
                if (!saved.ok()) return saved.error();
            }
        }

        config.app_id = creds->first;
        config.app_secret = creds->second;
    }
    return build_service(std::move(config));
}

Result<std::string> QobuzApiService::require_auth_token() const {
    if (!user_auth_token_) {
        return auth_error("Not authenticated. Call authenticate_with_getter() or login() first.");
    }
    return *user_auth_token_;
}

// --- Authentication

Result<std::pair<std::string, int64_t>> QobuzApiService::login(const std::string &email,
                                                               const std::string &password) {
    AE_LOGI("Attempting login");
    std::string hashed = ae::md5_hex(password);

    api::Params params{{"email", email}, {"password", hashed}};
    auto response = api::post<LoginResponse>(*api_client_, base_url_, "/user/login",
                                             std::move(params), app_id_, "");
    if (!response.ok()) return response.error();

    if (!response.value().user_auth_token) {
        return auth_error("Login succeeded but no user_auth_token in response");
    }
    std::string token = *response.value().user_auth_token;
    int64_t user_id = response.value().user_id.value_or(0);

    set_auth_token(token);
    AE_LOGI("Login successful");
    return std::make_pair(std::move(token), user_id);
}

Result<std::string> QobuzApiService::login_with_token(const std::string &user_id,
                                                      const std::string &auth_token) {
    AE_LOGI("Attempting token login");

    api::Params params{{"user_id", user_id}, {"user_auth_token", auth_token}};
    auto response = api::post<LoginResponse>(*api_client_, base_url_, "/user/login",
                                             std::move(params), app_id_, "");
    if (!response.ok()) return response.error();

    const LoginResponse &login = response.value();
    if (!login.user_auth_token) {
        return auth_error("Token login succeeded but no user_auth_token in response");
    }

    std::string country_code;
    if (login.user_id) {
        std::string returned_id = std::to_string(*login.user_id);
        if (returned_id != user_id) {
            return auth_error("User ID mismatch: requested " + user_id +
                              " but API returned " + returned_id);
        }
        country_code = login.country_code.value_or("");
    }

    set_auth_token(auth_token);
    AE_LOGI("Token login successful");
    return country_code;
}

Result<void> QobuzApiService::authenticate_with_getter(
    const std::function<std::optional<std::string>(const char *)> &get_env) {
    auto user_id = get_env("QOBUZ_USER_ID");
    auto token = get_env("QOBUZ_USER_AUTH_TOKEN");
    if (user_id && token) {
        AE_LOGI("Using token-based authentication");
        auto result = login_with_token(trim_copy(*user_id), trim_copy(*token));
        if (!result.ok()) return result.error();
        return {};
    }

    auto email = get_env("QOBUZ_EMAIL");
    if (!email) email = get_env("QOBUZ_USERNAME");
    if (!email) {
        return auth_error(
            "No QOBUZ_USER_ID/QOBUZ_USER_AUTH_TOKEN or QOBUZ_EMAIL/QOBUZ_PASSWORD "
            "credentials found");
    }
    auto password = get_env("QOBUZ_PASSWORD");
    if (!password) {
        return auth_error("QOBUZ_PASSWORD not found");
    }

    AE_LOGI("Using email/password authentication");
    auto result = login(trim_copy(*email), trim_copy(*password));
    if (!result.ok()) return result.error();
    return {};
}

Result<void> QobuzApiService::refresh_app_credentials() {
    if (credentials_refreshed_) {
        return credentials_error("Credentials can only be refreshed once per session");
    }

    AE_LOGI("Refreshing app credentials from web player");
    ae::HttpClient::Options opts;
    opts.user_agent = "Mozilla/5.0";
    opts.ca_bundle_path = ca_bundle_path_;
    ae::HttpClient plain(opts);

    auto extracted = extract_from_web_player(plain);
    if (!extracted.ok()) return extracted.error();

    if (!env_path_.empty()) {
        auto saved = save_app_credentials(env_path_, extracted.value().first,
                                          extracted.value().second);
        if (!saved.ok()) return saved.error();
    }

    app_id_ = extracted.value().first;
    app_secret_ = extracted.value().second;
    rebuild_http_client();
    credentials_refreshed_ = true;

    AE_LOGI("App credentials refreshed successfully");
    return {};
}

// --- Request helpers (content/mod.rs)

Result<std::string> QobuzApiService::do_signed_get_raw(const std::string &endpoint,
                                                       api::Params params) const {
    auto token = require_auth_token();
    if (!token.ok()) return token.error();
    return api::signed_get_raw(*api_client_, base_url_, endpoint, std::move(params),
                               request_auth(token.value()));
}

template <typename T>
Result<T> QobuzApiService::search(const std::string &endpoint, const std::string &query,
                                  std::optional<int> limit, std::optional<int> offset) const {
    api::Params params{{"query", query}};
    push_pagination_params(params, limit, offset);
    return do_signed_get<T>(endpoint, std::move(params));
}

template <typename T, typename Id>
Result<T> QobuzApiService::get_by_id(const std::string &endpoint, const char *id_field,
                                     const Id &id,
                                     const std::optional<std::string> &extra) const {
    api::Params params;
    if constexpr (std::is_convertible_v<Id, std::string>) {
        params.emplace_back(id_field, id);
    } else {
        params.emplace_back(id_field, std::to_string(id));
    }
    if (extra) params.emplace_back("extra", *extra);
    return do_signed_get<T>(endpoint, std::move(params));
}

// --- Search endpoints

Result<ItemSearchResult<std::shared_ptr<Album>>> QobuzApiService::search_albums(
    const std::string &query, std::optional<int> limit, std::optional<int> offset) const {
    auto resp = search<AlbumSearchResponse>("/album/search", query, limit, offset);
    if (!resp.ok()) return resp.error();
    return std::move(resp.value().albums);
}

Result<ItemSearchResult<std::shared_ptr<Artist>>> QobuzApiService::search_artists(
    const std::string &query, std::optional<int> limit, std::optional<int> offset) const {
    auto resp = search<ArtistSearchResponse>("/artist/search", query, limit, offset);
    if (!resp.ok()) return resp.error();
    return std::move(resp.value().artists);
}

Result<ItemSearchResult<std::shared_ptr<Track>>> QobuzApiService::search_tracks(
    const std::string &query, std::optional<int> limit, std::optional<int> offset) const {
    auto resp = search<TrackSearchResponse>("/track/search", query, limit, offset);
    if (!resp.ok()) return resp.error();
    return std::move(resp.value().tracks);
}

Result<ItemSearchResult<std::shared_ptr<Playlist>>> QobuzApiService::search_playlists(
    const std::string &query, std::optional<int> limit, std::optional<int> offset) const {
    auto resp = search<PlaylistSearchResponse>("/playlist/search", query, limit, offset);
    if (!resp.ok()) return resp.error();
    return std::move(resp.value().playlists);
}

Result<SearchResult> QobuzApiService::search_catalog(const std::string &query,
                                                     std::optional<int> limit,
                                                     std::optional<int> offset) const {
    // Rust runs the four searches concurrently with try_join!.
    auto albums_f = std::async(std::launch::async,
                               [&] { return search_albums(query, limit, offset); });
    auto artists_f = std::async(std::launch::async,
                                [&] { return search_artists(query, limit, offset); });
    auto tracks_f = std::async(std::launch::async,
                               [&] { return search_tracks(query, limit, offset); });
    auto playlists_f = std::async(std::launch::async,
                                  [&] { return search_playlists(query, limit, offset); });

    auto albums = albums_f.get();
    auto artists = artists_f.get();
    auto tracks = tracks_f.get();
    auto playlists = playlists_f.get();

    if (!albums.ok()) return albums.error();
    if (!artists.ok()) return artists.error();
    if (!tracks.ok()) return tracks.error();
    if (!playlists.ok()) return playlists.error();

    SearchResult result;
    result.albums = albums.take();
    result.artists = artists.take();
    result.tracks = tracks.take();
    result.playlists = playlists.take();
    return result;
}

// --- Browse endpoints

Result<Album> QobuzApiService::get_album(const std::string &album_id,
                                         const std::optional<std::string> &extra) const {
    return get_by_id<Album>("/album/get", "album_id", album_id, extra);
}

Result<Artist> QobuzApiService::get_artist(int artist_id,
                                           const std::optional<std::string> &extra) const {
    return get_by_id<Artist>("/artist/get", "artist_id", artist_id, extra);
}

Result<Track> QobuzApiService::get_track(int track_id) const {
    return get_by_id<Track>("/track/get", "track_id", track_id, std::nullopt);
}

Result<Playlist> QobuzApiService::get_playlist(const std::string &playlist_id,
                                               const std::optional<std::string> &extra) const {
    return get_by_id<Playlist>("/playlist/get", "playlist_id", playlist_id, extra);
}

Result<ItemSearchResult<std::shared_ptr<Album>>> QobuzApiService::get_release_list(
    int artist_id, std::optional<int> limit, std::optional<int> offset) const {
    api::Params params{{"artist_id", std::to_string(artist_id)}};
    push_pagination_params(params, limit, offset);
    return do_signed_get<ItemSearchResult<std::shared_ptr<Album>>>("/artist/getReleasesList",
                                                                   std::move(params));
}

// --- Favorites

Result<void> QobuzApiService::modify_favorites(const std::vector<int> &item_ids,
                                               const std::string &item_type,
                                               const std::string &endpoint) const {
    auto token = require_auth_token();
    if (!token.ok()) return token.error();

    std::string ids;
    for (size_t i = 0; i < item_ids.size(); ++i) {
        if (i) ids += ',';
        ids += std::to_string(item_ids[i]);
    }

    api::Params params{{"item_ids", ids}, {"item_type", item_type}};
    auto raw = api::signed_post_raw(*api_client_, base_url_, endpoint, std::move(params),
                                    request_auth(token.value()));
    if (!raw.ok()) return raw.error();
    return {};
}

Result<void> QobuzApiService::add_user_favorites(const std::vector<int> &item_ids,
                                                 const std::string &item_type) const {
    return modify_favorites(item_ids, item_type, "/favorite/create");
}

Result<void> QobuzApiService::delete_user_favorites(const std::vector<int> &item_ids,
                                                    const std::string &item_type) const {
    return modify_favorites(item_ids, item_type, "/favorite/delete");
}

Result<UserFavorites> QobuzApiService::fetch_user_favorites(api::Params params) const {
    return do_signed_get<UserFavorites>("/favorite/getUserFavorites", std::move(params));
}

Result<UserFavorites> QobuzApiService::get_user_favorites(const std::string &item_type,
                                                          std::optional<int> limit,
                                                          std::optional<int> offset) const {
    api::Params params{{"type", item_type}};
    push_pagination_params(params, limit, offset);
    return fetch_user_favorites(std::move(params));
}

Result<UserFavorites> QobuzApiService::get_user_favorite_ids() const {
    return fetch_user_favorites({{"type", "ids"}});
}

// --- Track file URL (tracks.rs)

Result<FileUrl> QobuzApiService::get_track_file_url(int track_id, int format_id) const {
    auto token = require_auth_token();
    if (!token.ok()) return token.error();
    return api::get_track_file_url_raw(*api_client_, base_url_,
                                       request_auth(token.value()), track_id, format_id);
}

} // namespace kb
