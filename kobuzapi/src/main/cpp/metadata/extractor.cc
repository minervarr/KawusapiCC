#include "extractor.hh"

namespace kb {

namespace {

// Replaces the last `600` size suffix with `org` for full resolution.
std::string upgrade_to_original_resolution(const std::string &url) {
    size_t pos = url.rfind("600");
    if (pos == std::string::npos) return url;
    return url.substr(0, pos) + "org" + url.substr(pos + 3);
}

} // namespace

std::optional<std::string> best_cover_url(const Image &image) {
    const std::optional<std::string> *candidates[] = {
        &image.mega, &image.extra_large, &image.large,
        &image.medium, &image.thumbnail, &image.small,
    };
    for (const auto *candidate : candidates) {
        if (*candidate) return upgrade_to_original_resolution(**candidate);
    }
    return std::nullopt;
}

ComprehensiveMetadata extract_comprehensive_metadata(const Track &track, const Album *album,
                                                     const Artist *artist) {
    ComprehensiveMetadata meta;

    meta.performer_name = track.performer ? track.performer->name : std::nullopt;
    if (!meta.performer_name && artist) meta.performer_name = artist->name;

    if (album && album->artists) {
        for (const auto &a : *album->artists) {
            if (!a) continue;
            meta.album_artists.push_back(AlbumArtistBrief{a->name, a->roles});
        }
    }

    meta.title = track.title;
    meta.track_version = track.version;
    if (album) {
        meta.album = album->title;
        meta.album_version = album->version;
        meta.album_artist_name = album->artist ? album->artist->name : std::nullopt;
        meta.album_composer_name = album->composer ? album->composer->name : std::nullopt;
        meta.genre = album->genre ? album->genre->name : std::nullopt;
        meta.album_release_date_download = album->release_date_download;
        meta.album_release_date_original = album->release_date_original;
        meta.released_at = album->released_at;
        meta.upc = album->upc;
        meta.product_url = album->product_url;
        meta.label = album->label ? album->label->name : std::nullopt;
        meta.release_type = album->release_type;
        meta.product_type = album->product_type;
        meta.track_total = album->tracks_count;
        meta.disc_total = album->media_count;
        if (album->image) meta.cover_art_url = best_cover_url(*album->image);
    }

    meta.performers = track.performers;
    meta.track_composer_name = track.composer ? track.composer->name : std::nullopt;
    meta.track_release_date_original = track.release_date_original;
    meta.copyright = track.copyright;
    meta.isrc = track.isrc;
    meta.track_number = track.track_number;
    meta.disc_number = track.media_number;
    meta.parental_warning = track.parental_warning;
    meta.is_classical = track.work.has_value();

    return meta;
}

} // namespace kb
