#pragma once

// Port of src/metadata/extractor.rs — metadata extraction from API models.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "../core/models.hh"

namespace kb {

struct AlbumArtistBrief {
    std::optional<std::string> name;
    std::optional<std::vector<std::string>> roles;
};

// Raw data the embedder processes with format-specific logic.
struct ComprehensiveMetadata {
    std::optional<std::string> title;
    std::optional<std::string> track_version;
    std::optional<std::string> album;
    std::optional<std::string> album_version;
    // Raw performers string ("Name, MainArtist - Other, Composer").
    std::optional<std::string> performers;
    std::optional<std::string> performer_name;
    std::optional<std::string> album_artist_name;
    std::vector<AlbumArtistBrief> album_artists;
    std::optional<std::string> album_composer_name;
    std::optional<std::string> track_composer_name;
    std::optional<std::string> genre;
    std::optional<std::string> album_release_date_download;
    std::optional<std::string> album_release_date_original;
    std::optional<std::string> track_release_date_original;
    std::optional<int64_t> released_at;
    std::optional<std::string> copyright;
    std::optional<std::string> isrc;
    std::optional<std::string> upc;
    std::optional<std::string> product_url;
    std::optional<std::string> label;
    std::optional<std::string> release_type;
    std::optional<std::string> product_type;
    std::optional<int> track_number;
    std::optional<int> track_total;
    std::optional<int> disc_number;
    std::optional<int> disc_total;
    std::optional<bool> parental_warning;
    bool is_classical = false;
    std::optional<std::string> cover_art_url;
    std::optional<std::vector<uint8_t>> cover_art_data;
};

// Highest-resolution cover URL, with the Qobuz `600` size suffix upgraded
// to `org` (original master resolution).
std::optional<std::string> best_cover_url(const Image &image);

ComprehensiveMetadata extract_comprehensive_metadata(const Track &track, const Album *album,
                                                     const Artist *artist);

} // namespace kb
