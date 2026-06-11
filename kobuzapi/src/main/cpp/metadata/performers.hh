#pragma once

// Port of src/metadata/embedder/performers.rs — performer string parsing
// and composer deduplication.

#include <set>
#include <string>
#include <utility>
#include <vector>

namespace kb {

// "Name, Role1, Role2 - Other, Role" -> [(name, [roles...]), ...]
std::vector<std::pair<std::string, std::vector<std::string>>> parse_performers(
    const std::string &performers_str);

// Names with Performer / *MainArtist* / *FeaturedArtist* roles, skipping
// `existing` and in-batch duplicates.
std::vector<std::string> extract_artist_names_from_performers(
    const std::string &performers_str, const std::set<std::string> &existing);

// Names with *Composer* / *Lyricist* roles, deduplicated.
std::vector<std::string> extract_composers_from_performers(const std::string &performers_str);

// Names with an exact "Producer" role.
std::vector<std::string> extract_producers_from_performers(const std::string &performers_str);

// Lowercased, punctuation-normalized name for comparisons.
std::string normalize_composer_name(const std::string &name);

// True when the normalized name matches (or substring-matches) an entry.
bool is_duplicate_composer(const std::string &name, const std::set<std::string> &existing);

} // namespace kb
