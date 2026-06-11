#pragma once

// Port of src/metadata/config.rs — field toggles for metadata embedding.

#include <bitset>
#include <cstddef>

namespace kb {

enum class MetadataField : size_t {
    Title,
    Artist,
    Album,
    AlbumArtist,
    Genre,
    ReleaseDate,
    ReleaseYear,
    Composer,
    TrackNumber,
    TrackTotal,
    DiscNumber,
    DiscTotal,
    CoverArt,
    Isrc,
    Copyright,
    Label,
    MediaType,
    Comment,
    Producer,
    InvolvedPeople,
    Explicit,
    Upc,
    Url,
    Count_, // sentinel
};

// Default: everything enabled except Comment (matching the Rust Default).
class MetadataConfig {
public:
    static MetadataConfig all() {
        MetadataConfig c;
        c.enabled_.set();
        return c;
    }

    MetadataConfig() {
        enabled_.set();
        set(MetadataField::Comment, false);
    }

    bool is_enabled(MetadataField field) const {
        return enabled_.test(static_cast<size_t>(field));
    }
    void set(MetadataField field, bool enabled) {
        enabled_.set(static_cast<size_t>(field), enabled);
    }

private:
    std::bitset<static_cast<size_t>(MetadataField::Count_)> enabled_;
};

} // namespace kb
