#include "json.hh"

#include <stdexcept>

namespace kb {

using nlohmann::json;
using jsonu::get_opt;
using jsonu::present;

namespace jsonu {

std::optional<std::string> flexible_name(const json &j, const char *key) {
    if (!present(j, key)) return std::nullopt;
    const json &v = j.at(key);
    if (v.is_string()) return v.get<std::string>();
    if (v.is_object()) {
        auto it = v.find("display");
        if (it != v.end() && it->is_string()) return it->get<std::string>();
        throw std::runtime_error("object missing 'display' field");
    }
    return v.dump();
}

std::optional<std::string> flexible_string_id(const json &j, const char *key) {
    if (!present(j, key)) return std::nullopt;
    const json &v = j.at(key);
    if (v.is_string()) return v.get<std::string>();
    return v.dump();
}

std::optional<Image> picture(const json &j, const char *key) {
    if (!present(j, key)) return std::nullopt;
    const json &v = j.at(key);
    if (v.is_string()) return std::nullopt;
    return v.get<Image>();
}

} // namespace jsonu

void from_json(const json &j, Image &v) {
    get_opt(j, "small", v.small);
    get_opt(j, "thumbnail", v.thumbnail);
    get_opt(j, "medium", v.medium);
    get_opt(j, "large", v.large);
    get_opt(j, "extralarge", v.extra_large);
    get_opt(j, "mega", v.mega);
    get_opt(j, "back", v.back);
    get_opt(j, "url", v.url);
}

void from_json(const json &j, Label &v) {
    get_opt(j, "id", v.id);
    get_opt(j, "name", v.name);
    get_opt(j, "slug", v.slug);
}

void from_json(const json &j, Genre &v) {
    get_opt(j, "id", v.id);
    get_opt(j, "name", v.name);
    get_opt(j, "slug", v.slug);
    get_opt(j, "color", v.color);
}

void from_json(const json &j, Biography &v) {
    get_opt(j, "text", v.text);
    get_opt(j, "summary", v.summary);
}

void from_json(const json &j, Artist &v) {
    get_opt(j, "id", v.id);
    v.name = jsonu::flexible_name(j, "name");
    get_opt(j, "slug", v.slug);
    v.picture = jsonu::picture(j, "picture");
    get_opt(j, "image", v.image);
    get_opt(j, "biography", v.biography);
    get_opt(j, "albums_count", v.albums_count);
    get_opt(j, "roles", v.roles);
    get_opt(j, "albums", v.albums);
}

void from_json(const json &j, AudioInfo &v) {
    get_opt(j, "bit_depth", v.bit_depth);
    get_opt(j, "sampling_rate", v.sampling_rate);
    get_opt(j, "channels", v.channels);
    get_opt(j, "codec", v.codec);
}

void from_json(const json &j, Album &v) {
    get_opt(j, "id", v.id);
    get_opt(j, "title", v.title);
    get_opt(j, "version", v.version);
    get_opt(j, "upc", v.upc);
    get_opt(j, "url", v.url);
    get_opt(j, "artist", v.artist);
    get_opt(j, "artists", v.artists);
    get_opt(j, "composer", v.composer);
    get_opt(j, "label", v.label);
    get_opt(j, "genre", v.genre);
    get_opt(j, "genres", v.genres);
    get_opt(j, "image", v.image);
    get_opt(j, "duration", v.duration);
    get_opt(j, "tracks_count", v.tracks_count);
    get_opt(j, "media_count", v.media_count);
    get_opt(j, "release_date_original", v.release_date_original);
    get_opt(j, "release_date_stream", v.release_date_stream);
    get_opt(j, "release_date_download", v.release_date_download);
    get_opt(j, "product_type", v.product_type);
    get_opt(j, "release_type", v.release_type);
    get_opt(j, "hires", v.hires);
    get_opt(j, "hires_streamable", v.hires_streamable);
    get_opt(j, "downloadable", v.downloadable);
    get_opt(j, "streamable", v.streamable);
    get_opt(j, "parental_warning", v.parental_warning);
    get_opt(j, "track_ids", v.track_ids);
    get_opt(j, "tracks", v.tracks);
    get_opt(j, "product_url", v.product_url);
    get_opt(j, "released_at", v.released_at);
    get_opt(j, "copyright", v.copyright);
    v.product_sales_factors =
        present(j, "product_sales_factors") ? j.at("product_sales_factors") : json();
    get_opt(j, "maximum_bit_depth", v.maximum_bit_depth);
    get_opt(j, "maximum_sampling_rate", v.maximum_sampling_rate);
    get_opt(j, "maximum_channel_count", v.maximum_channel_count);
}

void from_json(const json &j, Track &v) {
    get_opt(j, "id", v.id);
    get_opt(j, "title", v.title);
    get_opt(j, "version", v.version);
    get_opt(j, "isrc", v.isrc);
    get_opt(j, "track_number", v.track_number);
    get_opt(j, "duration", v.duration);
    get_opt(j, "media_number", v.media_number);
    get_opt(j, "work", v.work);
    get_opt(j, "album", v.album);
    get_opt(j, "performer", v.performer);
    get_opt(j, "performers", v.performers);
    get_opt(j, "composer", v.composer);
    get_opt(j, "audio_info", v.audio_info);
    get_opt(j, "copyright", v.copyright);
    get_opt(j, "streamable", v.streamable);
    get_opt(j, "downloadable", v.downloadable);
    get_opt(j, "hires", v.hires);
    get_opt(j, "maximum_bit_depth", v.maximum_bit_depth);
    get_opt(j, "maximum_sampling_rate", v.maximum_sampling_rate);
    get_opt(j, "maximum_channel_count", v.maximum_channel_count);
    get_opt(j, "release_date_original", v.release_date_original);
    get_opt(j, "release_date_stream", v.release_date_stream);
    get_opt(j, "parental_warning", v.parental_warning);
    v.product_sales_factors =
        present(j, "product_sales_factors") ? j.at("product_sales_factors") : json();
}

void from_json(const json &j, Credential &v) {
    v.user_id = jsonu::flexible_string_id(j, "user_id");
    get_opt(j, "user_auth_token", v.user_auth_token);
    get_opt(j, "email", v.email);
    get_opt(j, "password", v.password);
    get_opt(j, "app_id", v.app_id);
    get_opt(j, "app_secret", v.app_secret);
}

void from_json(const json &j, Subscription &v) {
    get_opt(j, "id", v.id);
    get_opt(j, "offer", v.offer);
    get_opt(j, "start_date", v.start_date);
    get_opt(j, "end_date", v.end_date);
    get_opt(j, "status", v.status);
    get_opt(j, "is_active", v.is_active);
}

void from_json(const json &j, User &v) {
    get_opt(j, "id", v.id);
    get_opt(j, "credential", v.credential);
    get_opt(j, "subscription", v.subscription);
    get_opt(j, "display_name", v.display_name);
}

void from_json(const json &j, PlaylistOwner &v) {
    get_opt(j, "id", v.id);
    get_opt(j, "name", v.name);
}

void from_json(const json &j, Playlist &v) {
    v.id = jsonu::flexible_string_id(j, "id");
    get_opt(j, "name", v.name);
    get_opt(j, "description", v.description);
    get_opt(j, "tracks_count", v.tracks_count);
    get_opt(j, "duration", v.duration);
    get_opt(j, "is_public", v.is_public);
    get_opt(j, "creator", v.creator);
    get_opt(j, "owner", v.owner);
    get_opt(j, "image", v.image);
    get_opt(j, "image_rectangle", v.image_rectangle);
    get_opt(j, "image_rectangle_mini", v.image_rectangle_mini);
    get_opt(j, "images", v.images);
    get_opt(j, "images150", v.images150);
    get_opt(j, "images300", v.images300);
    get_opt(j, "tracks", v.tracks);
    get_opt(j, "created_at", v.created_at);
    get_opt(j, "updated_at", v.updated_at);
}

void from_json(const json &j, FileUrl &v) {
    get_opt(j, "track_id", v.track_id);
    get_opt(j, "duration", v.duration);
    get_opt(j, "url", v.url);
    get_opt(j, "format_id", v.format_id);
    get_opt(j, "mime_type", v.mime_type);
    get_opt(j, "sampling_rate", v.sampling_rate);
    get_opt(j, "bit_depth", v.bit_depth);
    get_opt(j, "status", v.status);
    get_opt(j, "message", v.message);
    v.code = jsonu::flexible_string_id(j, "code");
}

void from_json(const json &j, AlbumSearchResponse &v) { j.at("albums").get_to(v.albums); }
void from_json(const json &j, ArtistSearchResponse &v) { j.at("artists").get_to(v.artists); }
void from_json(const json &j, TrackSearchResponse &v) { j.at("tracks").get_to(v.tracks); }
void from_json(const json &j, PlaylistSearchResponse &v) {
    j.at("playlists").get_to(v.playlists);
}

void from_json(const json &j, SearchResult &v) {
    get_opt(j, "albums", v.albums);
    get_opt(j, "artists", v.artists);
    get_opt(j, "tracks", v.tracks);
    get_opt(j, "playlists", v.playlists);
}

void from_json(const json &j, UserFavorites &v) {
    get_opt(j, "albums", v.albums);
    get_opt(j, "artists", v.artists);
    get_opt(j, "tracks", v.tracks);
    get_opt(j, "article_ids", v.article_ids);
    get_opt(j, "artist_ids", v.artist_ids);
    get_opt(j, "album_ids", v.album_ids);
    get_opt(j, "track_ids", v.track_ids);
}

} // namespace kb
