#pragma once

// Port of src/models/* — every field optional, mirroring the Rust models.
// Box<T> fields become std::shared_ptr<T> (null = absent); Option<T> fields
// become std::optional<T>. Raw serde_json::Value passthroughs are stored as
// nlohmann::json (null = absent).

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace kb {

// Quality format IDs for track downloads (src/models/file_url.rs).
namespace quality {
inline constexpr int MP3_320 = 5;
inline constexpr int FLAC_16_44 = 6;
inline constexpr int FLAC_24_96 = 7;
inline constexpr int FLAC_24_192 = 27;
} // namespace quality

struct Album;
struct Artist;
struct Track;
struct Playlist;

// Generic paginated result container.
template <typename T>
struct ItemSearchResult {
    std::optional<std::vector<T>> items;
    std::optional<int> total;
    std::optional<int> limit;
    std::optional<int> offset;
};

struct Image {
    std::optional<std::string> small;
    std::optional<std::string> thumbnail;
    std::optional<std::string> medium;
    std::optional<std::string> large;
    std::optional<std::string> extra_large; // JSON key "extralarge"
    std::optional<std::string> mega;
    std::optional<std::string> back;
    std::optional<std::string> url;
};

struct Label {
    std::optional<int> id;
    std::optional<std::string> name;
    std::optional<std::string> slug;
};

struct Genre {
    std::optional<int> id;
    std::optional<std::string> name;
    std::optional<std::string> slug;
    std::optional<std::string> color;
};

struct Biography {
    std::optional<std::string> text;
    std::optional<std::string> summary;
};

struct Artist {
    std::optional<int> id;
    // Flexible: plain string or {"display": "Name"}.
    std::optional<std::string> name;
    std::optional<std::string> slug;
    // Flexible: string URL (dropped) or Image object.
    std::optional<Image> picture;
    std::optional<Image> image;
    std::optional<Biography> biography;
    std::optional<int> albums_count;
    std::optional<std::vector<std::string>> roles;
    std::optional<ItemSearchResult<std::shared_ptr<Album>>> albums;
};

struct AudioInfo {
    std::optional<int> bit_depth;
    std::optional<double> sampling_rate;
    std::optional<int> channels;
    std::optional<std::string> codec;
};

struct Album {
    std::optional<std::string> id;
    std::optional<std::string> title;
    std::optional<std::string> version;
    std::optional<std::string> upc;
    std::optional<std::string> url;
    std::shared_ptr<Artist> artist;
    std::optional<std::vector<std::shared_ptr<Artist>>> artists;
    std::shared_ptr<Artist> composer;
    std::optional<Label> label;
    std::optional<Genre> genre;
    std::optional<std::vector<Genre>> genres;
    std::optional<Image> image;
    std::optional<int> duration;
    std::optional<int> tracks_count;
    std::optional<int> media_count;
    std::optional<std::string> release_date_original;
    std::optional<std::string> release_date_stream;
    std::optional<std::string> release_date_download;
    std::optional<std::string> product_type;
    std::optional<std::string> release_type;
    std::optional<bool> hires;
    std::optional<bool> hires_streamable;
    std::optional<bool> downloadable;
    std::optional<bool> streamable;
    std::optional<bool> parental_warning;
    std::optional<std::vector<int>> track_ids;
    std::optional<ItemSearchResult<std::shared_ptr<Track>>> tracks;
    std::optional<std::string> product_url;
    std::optional<int64_t> released_at;
    std::optional<std::string> copyright;
    nlohmann::json product_sales_factors; // raw passthrough, null = absent
    std::optional<int> maximum_bit_depth;
    std::optional<double> maximum_sampling_rate;
    std::optional<int> maximum_channel_count;

    // 5 = MP3, everything else = FLAC.
    static const char *extension_for_format(int format_id) {
        return format_id == 5 ? "mp3" : "flac";
    }
};

struct Track {
    std::optional<int> id;
    std::optional<std::string> title;
    std::optional<std::string> version;
    std::optional<std::string> isrc;
    std::optional<int> track_number;
    std::optional<int> duration;
    std::optional<int> media_number;
    std::optional<std::string> work;
    std::shared_ptr<Album> album;
    std::shared_ptr<Artist> performer;
    std::optional<std::string> performers;
    std::shared_ptr<Artist> composer;
    std::optional<AudioInfo> audio_info;
    std::optional<std::string> copyright;
    std::optional<bool> streamable;
    std::optional<bool> downloadable;
    std::optional<bool> hires;
    std::optional<int> maximum_bit_depth;
    std::optional<double> maximum_sampling_rate;
    std::optional<int> maximum_channel_count;
    std::optional<std::string> release_date_original;
    std::optional<std::string> release_date_stream;
    std::optional<bool> parental_warning;
    nlohmann::json product_sales_factors;
};

struct Credential {
    std::optional<std::string> user_id;
    std::optional<std::string> user_auth_token;
    std::optional<std::string> email;
    std::optional<std::string> password; // MD5-hashed
    std::optional<std::string> app_id;
    std::optional<std::string> app_secret;
};

struct Subscription {
    std::optional<int> id;
    std::optional<std::string> offer;
    std::optional<std::string> start_date;
    std::optional<std::string> end_date;
    std::optional<std::string> status;
    std::optional<bool> is_active;
};

struct User {
    std::optional<int> id;
    std::optional<Credential> credential;
    std::optional<Subscription> subscription;
    std::optional<std::string> display_name;
};

struct PlaylistOwner {
    std::optional<int> id;
    std::optional<std::string> name;
};

struct Playlist {
    // Flexible: string or number from the API.
    std::optional<std::string> id;
    std::optional<std::string> name;
    std::optional<std::string> description;
    std::optional<int> tracks_count;
    std::optional<int> duration;
    std::optional<bool> is_public;
    std::optional<User> creator;
    std::optional<PlaylistOwner> owner;
    std::optional<Image> image;
    std::optional<std::vector<std::string>> image_rectangle;
    std::optional<std::vector<std::string>> image_rectangle_mini;
    std::optional<std::vector<std::string>> images;
    std::optional<std::vector<std::string>> images150;
    std::optional<std::vector<std::string>> images300;
    std::optional<ItemSearchResult<std::shared_ptr<Track>>> tracks;
    std::optional<int64_t> created_at;
    std::optional<int64_t> updated_at;

    // creator.display_name (from /playlist/get) first, then owner.name
    // (from search endpoints).
    std::optional<std::string> creator_name() const {
        if (creator && creator->display_name) return creator->display_name;
        if (owner && owner->name) return owner->name;
        return std::nullopt;
    }

    std::optional<std::string> best_image_url(bool large) const {
        auto first = [](const std::optional<std::vector<std::string>> &v)
            -> std::optional<std::string> {
            if (v && !v->empty()) return v->front();
            return std::nullopt;
        };
        if (large) {
            if (auto u = first(image_rectangle)) return u;
            if (auto u = first(images300)) return u;
            if (auto u = first(images150)) return u;
            return first(images);
        }
        if (auto u = first(image_rectangle_mini)) return u;
        if (auto u = first(image_rectangle)) return u;
        if (auto u = first(images150)) return u;
        return first(images);
    }
};

struct FileUrl {
    std::optional<int> track_id;
    std::optional<double> duration;
    std::optional<std::string> url;
    std::optional<int> format_id;
    std::optional<std::string> mime_type;
    std::optional<double> sampling_rate;
    std::optional<int> bit_depth;
    std::optional<int> status;
    std::optional<std::string> message;
    std::optional<std::string> code;
};

struct AlbumSearchResponse {
    ItemSearchResult<std::shared_ptr<Album>> albums;
};

struct ArtistSearchResponse {
    ItemSearchResult<std::shared_ptr<Artist>> artists;
};

struct TrackSearchResponse {
    ItemSearchResult<std::shared_ptr<Track>> tracks;
};

struct PlaylistSearchResponse {
    ItemSearchResult<std::shared_ptr<Playlist>> playlists;
};

struct SearchResult {
    std::optional<ItemSearchResult<std::shared_ptr<Album>>> albums;
    std::optional<ItemSearchResult<std::shared_ptr<Artist>>> artists;
    std::optional<ItemSearchResult<std::shared_ptr<Track>>> tracks;
    std::optional<ItemSearchResult<std::shared_ptr<Playlist>>> playlists;
};

struct UserFavorites {
    std::optional<ItemSearchResult<std::shared_ptr<Album>>> albums;
    std::optional<ItemSearchResult<std::shared_ptr<Artist>>> artists;
    std::optional<ItemSearchResult<std::shared_ptr<Track>>> tracks;
    std::optional<std::vector<int>> article_ids;
    std::optional<std::vector<int>> artist_ids;
    std::optional<std::vector<int>> album_ids;
    std::optional<std::vector<int>> track_ids;
};

} // namespace kb
