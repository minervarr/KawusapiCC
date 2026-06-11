#include "embedder.hh"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>

#include "ae/tag.hh"
#include "performers.hh"

namespace kb {

namespace {

bool ends_with_icase(const std::string &s, const char *suffix) {
    std::string lower;
    for (char c : s) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string suf(suffix);
    return lower.size() >= suf.size() &&
           lower.compare(lower.size() - suf.size(), suf.size(), suf) == 0;
}

std::string trim(const std::string &s) {
    size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

std::string replace_all(std::string s, const std::string &from, const std::string &to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string join(const std::vector<std::string> &v, const std::string &sep) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += sep;
        out += v[i];
    }
    return out;
}

void push(ae::TagData &tags, const char *key, std::string value) {
    tags.fields.emplace_back(key, std::move(value));
}

// --- basic_fields.rs

void apply_title(ae::TagData &tags, const ComprehensiveMetadata &meta,
                 const MetadataConfig &config) {
    if (!config.is_enabled(MetadataField::Title) || !meta.title) return;
    std::string full = *meta.title;
    if (meta.track_version && !meta.track_version->empty()) {
        full += " (" + *meta.track_version + ")";
    }
    push(tags, "TITLE", std::move(full));
}

void apply_album(ae::TagData &tags, const ComprehensiveMetadata &meta,
                 const MetadataConfig &config) {
    if (!config.is_enabled(MetadataField::Album) || !meta.album) return;
    std::string full = *meta.album;
    if (meta.album_version && !meta.album_version->empty()) {
        full += " (" + *meta.album_version + ")";
    }
    push(tags, "ALBUM", std::move(full));
}

void apply_label(ae::TagData &tags, const ComprehensiveMetadata &meta,
                 const MetadataConfig &config) {
    if (!config.is_enabled(MetadataField::Label) || !meta.label) return;
    push(tags, "LABEL", *meta.label);
}

void apply_genre(ae::TagData &tags, const ComprehensiveMetadata &meta,
                 const MetadataConfig &config) {
    if (!config.is_enabled(MetadataField::Genre) || !meta.genre) return;
    push(tags, "GENRE", *meta.genre);
}

void apply_track_numbers(ae::TagData &tags, const ComprehensiveMetadata &meta,
                         const MetadataConfig &config) {
    if (config.is_enabled(MetadataField::TrackNumber) && meta.track_number) {
        push(tags, "TRACKNUMBER", std::to_string(*meta.track_number));
    }
    if (config.is_enabled(MetadataField::TrackTotal) && meta.track_total) {
        push(tags, "TRACKTOTAL", std::to_string(*meta.track_total));
    }
}

void apply_disc_numbers(ae::TagData &tags, const ComprehensiveMetadata &meta,
                        const MetadataConfig &config) {
    if (config.is_enabled(MetadataField::DiscNumber) && meta.disc_number) {
        push(tags, "DISCNUMBER", std::to_string(*meta.disc_number));
    }
    if (config.is_enabled(MetadataField::DiscTotal) && meta.disc_total) {
        push(tags, "DISCTOTAL", std::to_string(*meta.disc_total));
    }
}

void apply_copyright(ae::TagData &tags, const ComprehensiveMetadata &meta,
                     const MetadataConfig &config) {
    if (!config.is_enabled(MetadataField::Copyright) || !meta.copyright) return;
    push(tags, "COPYRIGHT", *meta.copyright);
}

void apply_isrc(ae::TagData &tags, const ComprehensiveMetadata &meta,
                const MetadataConfig &config) {
    if (!config.is_enabled(MetadataField::Isrc) || !meta.isrc) return;
    push(tags, "ISRC", *meta.isrc);
}

std::optional<unsigned> parse_year(const std::string &date) {
    std::string head = date.substr(0, date.find('-'));
    if (head.empty()) return std::nullopt;
    for (char c : head) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return std::nullopt;
    }
    return static_cast<unsigned>(std::stoul(head));
}

// Civil-from-days conversion, identical to the Rust port.
std::pair<std::optional<std::string>, std::optional<unsigned>> timestamp_to_date_and_year(
    int64_t timestamp) {
    int64_t days = timestamp / 86400 - ((timestamp % 86400 < 0) ? 1 : 0);
    int64_t z = days + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t y = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    int64_t d = doy - (153 * mp + 2) / 5 + 1;
    int64_t m = (mp < 10) ? mp + 3 : mp - 9;
    if (m <= 2) y += 1;

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04lld-%02lld-%02lld", static_cast<long long>(y),
                  static_cast<long long>(m), static_cast<long long>(d));
    return {std::string(buf), y > 0 ? std::optional<unsigned>(static_cast<unsigned>(y))
                                    : std::optional<unsigned>(0u)};
}

// Date priority: album download > album original > track original > released_at.
std::pair<std::optional<std::string>, std::optional<unsigned>> determine_primary_date(
    const ComprehensiveMetadata &meta) {
    for (const auto *d : {&meta.album_release_date_download,
                          &meta.album_release_date_original,
                          &meta.track_release_date_original}) {
        if (*d) return {**d, parse_year(**d)};
    }
    if (meta.released_at) return timestamp_to_date_and_year(*meta.released_at);
    return {std::nullopt, std::nullopt};
}

// FLAC: YEAR + DATE. MP3: DATE (TDRC) carries the year, RELEASEDATE (TDRL)
// the full date — mirroring the lofty ItemKey selection.
void apply_dates(ae::TagData &tags, const ComprehensiveMetadata &meta,
                 const MetadataConfig &config, bool is_flac) {
    auto [date_full, year] = determine_primary_date(meta);
    if (config.is_enabled(MetadataField::ReleaseYear) && year) {
        push(tags, is_flac ? "YEAR" : "DATE", std::to_string(*year));
    }
    if (!config.is_enabled(MetadataField::ReleaseDate)) return;
    if (date_full) {
        push(tags, is_flac ? "DATE" : "RELEASEDATE", *date_full);
    }
}

std::string normalize_qobuz_url(const std::string &url) {
    std::string full = (url.rfind("http", 0) == 0) ? url : "https://www.qobuz.com" + url;
    const std::string prefix = "https://www.qobuz.com/";
    if (full.rfind(prefix, 0) == 0) {
        std::string rest = full.substr(prefix.size());
        size_t slash = rest.find('/');
        if (slash != std::string::npos) {
            std::string locale = rest.substr(0, slash);
            if (locale.size() == 5 && locale[2] == '-') {
                return "https://open.qobuz.com/" + rest.substr(slash + 1);
            }
        }
    }
    return full;
}

void apply_url(ae::TagData &tags, const ComprehensiveMetadata &meta,
               const MetadataConfig &config, bool is_flac) {
    if (!config.is_enabled(MetadataField::Url) || !meta.product_url) return;
    // FLAC gets the custom URL key (apply_flac_custom_keys); MP3 gets a
    // commercial-information key (TXXX frame under TagLib).
    if (!is_flac) {
        push(tags, "COMMERCIALINFORMATIONURL", normalize_qobuz_url(*meta.product_url));
    }
}

void apply_media_type(ae::TagData &tags, const ComprehensiveMetadata &meta,
                      const MetadataConfig &config) {
    if (!config.is_enabled(MetadataField::MediaType)) return;
    const std::optional<std::string> &media =
        meta.release_type ? meta.release_type : meta.product_type;
    if (media) push(tags, "MEDIA", *media);
}

void apply_flac_custom_keys(ae::TagData &tags, const ComprehensiveMetadata &meta,
                            const MetadataConfig &config) {
    if (config.is_enabled(MetadataField::InvolvedPeople) && meta.performers &&
        !meta.performers->empty()) {
        std::string formatted =
            trim(replace_all(replace_all(*meta.performers, "\r\n", ". "), "\r", ". "));
        push(tags, "INVOLVEDPEOPLE", std::move(formatted));
    }
    if (config.is_enabled(MetadataField::Explicit) && meta.parental_warning) {
        push(tags, "ITUNESADVISORY", *meta.parental_warning ? "1" : "0");
    }
    if (config.is_enabled(MetadataField::Label) && meta.label) {
        push(tags, "ORGANIZATION", *meta.label);
    }
    if (config.is_enabled(MetadataField::Upc) && meta.upc) {
        push(tags, "UPC", *meta.upc);
    }
    if (config.is_enabled(MetadataField::Url) && meta.product_url) {
        push(tags, "URL", normalize_qobuz_url(*meta.product_url));
    }
}

// --- artist_fields.rs

// FLAC: prefers a main-artist conductor named "<name>, Conductor" in the
// performers string; otherwise the album's primary artist.
std::string build_flac_album_artist(const ComprehensiveMetadata &meta) {
    if (meta.performers) {
        for (const auto &a : meta.album_artists) {
            bool has_main_role = a.roles && std::find(a.roles->begin(), a.roles->end(),
                                                      "main-artist") != a.roles->end();
            bool matches_conductor =
                a.name &&
                meta.performers->find(*a.name + ", Conductor") != std::string::npos;
            if (has_main_role && matches_conductor) return a.name.value_or("");
        }
    }
    return meta.album_artist_name.value_or("");
}

// MP3: all main-artist names slash-separated.
std::string build_mp3_album_artist(const ComprehensiveMetadata &meta) {
    std::vector<std::string> main;
    for (const auto &a : meta.album_artists) {
        bool has_role = a.roles && std::find(a.roles->begin(), a.roles->end(),
                                             "main-artist") != a.roles->end();
        if (has_role && a.name) main.push_back(*a.name);
    }
    if (main.empty() && meta.album_artist_name) main.push_back(*meta.album_artist_name);
    return join(main, "/");
}

void apply_album_artist(ae::TagData &tags, const ComprehensiveMetadata &meta,
                        const MetadataConfig &config, bool is_flac) {
    if (!config.is_enabled(MetadataField::AlbumArtist)) return;
    std::string name = is_flac ? build_flac_album_artist(meta) : build_mp3_album_artist(meta);
    if (!name.empty()) push(tags, "ALBUMARTIST", std::move(name));
}

void apply_artist(ae::TagData &tags, const ComprehensiveMetadata &meta,
                  const MetadataConfig &config, bool is_flac) {
    if (!config.is_enabled(MetadataField::Artist)) return;

    std::vector<std::string> names;
    std::set<std::string> seen;
    if (meta.performers) {
        for (auto &name : extract_artist_names_from_performers(*meta.performers, seen)) {
            if (!seen.count(name)) {
                names.push_back(name);
                seen.insert(name);
            }
        }
    }
    if (meta.performer_name && !seen.count(*meta.performer_name)) {
        names.push_back(*meta.performer_name);
        seen.insert(*meta.performer_name);
    }
    for (const auto &a : meta.album_artists) {
        if (a.name && !a.name->empty() && !seen.count(*a.name)) {
            names.push_back(*a.name);
            seen.insert(*a.name);
        }
    }
    if (!names.empty()) {
        push(tags, "ARTIST", join(names, is_flac ? ", " : "/"));
    }
}

std::vector<std::string> get_composer_fallback(const ComprehensiveMetadata &meta) {
    if (meta.track_composer_name && *meta.track_composer_name != "Various Composers") {
        return {*meta.track_composer_name};
    }
    if (meta.album_composer_name && *meta.album_composer_name != "Various Composers") {
        return {*meta.album_composer_name};
    }
    return {};
}

// FLAC: the last composer from the performers string.
std::vector<std::string> build_flac_composers(const ComprehensiveMetadata &meta) {
    std::vector<std::string> from_performers;
    if (meta.performers) {
        from_performers = extract_composers_from_performers(*meta.performers);
    }
    if (!from_performers.empty() && from_performers.back() != "Various Composers") {
        return {from_performers.back()};
    }
    return get_composer_fallback(meta);
}

// MP3: performer-derived composers with fuzzy dedup, fallback otherwise.
std::vector<std::string> build_mp3_composers(const ComprehensiveMetadata &meta) {
    std::vector<std::string> composers;
    std::set<std::string> normalized;
    if (meta.performers) {
        for (auto &c : extract_composers_from_performers(*meta.performers)) {
            if (c != "Various Composers" && !is_duplicate_composer(c, normalized)) {
                composers.push_back(c);
                normalized.insert(normalize_composer_name(c));
            }
        }
    }
    if (!composers.empty()) return composers;
    return get_composer_fallback(meta);
}

void apply_composer(ae::TagData &tags, const ComprehensiveMetadata &meta,
                    const MetadataConfig &config, bool is_flac) {
    if (!config.is_enabled(MetadataField::Composer)) return;
    auto composers = is_flac ? build_flac_composers(meta) : build_mp3_composers(meta);
    if (!composers.empty()) push(tags, "COMPOSER", join(composers, "/"));
}

void apply_producer(ae::TagData &tags, const ComprehensiveMetadata &meta,
                    const MetadataConfig &config, bool is_flac) {
    if (!config.is_enabled(MetadataField::Producer) || !is_flac || !meta.performers) return;
    for (auto &producer : extract_producers_from_performers(*meta.performers)) {
        push(tags, "PRODUCER", std::move(producer));
    }
}

void apply_cover_art(ae::TagData &tags, const ComprehensiveMetadata &meta,
                     const MetadataConfig &config) {
    if (!config.is_enabled(MetadataField::CoverArt) || !meta.cover_art_data) return;
    ae::CoverArt cover;
    cover.mime_type = "image/jpeg";
    cover.data = *meta.cover_art_data;
    tags.cover = std::move(cover);
}

} // namespace

Result<void> embed_metadata_in_file(const std::string &path,
                                    const ComprehensiveMetadata &meta,
                                    const MetadataConfig &config) {
    bool is_flac = !ends_with_icase(path, ".mp3");

    ae::TagData tags;
    tags.clear_existing = true;

    apply_title(tags, meta, config);
    apply_album(tags, meta, config);
    apply_album_artist(tags, meta, config, is_flac);
    apply_artist(tags, meta, config, is_flac);
    apply_composer(tags, meta, config, is_flac);
    apply_producer(tags, meta, config, is_flac);
    apply_label(tags, meta, config);
    apply_genre(tags, meta, config);
    apply_track_numbers(tags, meta, config);
    apply_disc_numbers(tags, meta, config);
    apply_copyright(tags, meta, config);
    apply_isrc(tags, meta, config);
    apply_dates(tags, meta, config, is_flac);
    apply_url(tags, meta, config, is_flac);
    apply_media_type(tags, meta, config);
    apply_cover_art(tags, meta, config);
    if (is_flac) apply_flac_custom_keys(tags, meta, config);

    auto result = ae::write_tags(path, tags);
    if (!result.ok()) return from_engine(result.error());
    return {};
}

} // namespace kb
