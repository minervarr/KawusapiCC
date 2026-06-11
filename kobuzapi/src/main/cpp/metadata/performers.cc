#include "performers.hh"

#include <algorithm>
#include <cctype>

namespace kb {

namespace {

std::string trim(const std::string &s) {
    size_t begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

std::vector<std::string> split(const std::string &s, const std::string &sep) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        size_t pos = s.find(sep, start);
        if (pos == std::string::npos) {
            out.push_back(s.substr(start));
            return out;
        }
        out.push_back(s.substr(start, pos - start));
        start = pos + sep.size();
    }
}

bool contains(const std::string &haystack, const char *needle) {
    return haystack.find(needle) != std::string::npos;
}

bool vec_contains(const std::vector<std::string> &v, const std::string &s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

} // namespace

std::vector<std::pair<std::string, std::vector<std::string>>> parse_performers(
    const std::string &performers_str) {
    std::vector<std::pair<std::string, std::vector<std::string>>> out;
    for (const auto &group_raw : split(performers_str, " - ")) {
        std::string group = trim(group_raw);
        std::vector<std::string> parts;
        for (const auto &p : split(group, ",")) parts.push_back(trim(p));
        if (parts.empty()) continue;
        std::string person = parts.front();
        parts.erase(parts.begin());
        out.emplace_back(std::move(person), std::move(parts));
    }
    return out;
}

std::vector<std::string> extract_artist_names_from_performers(
    const std::string &performers_str, const std::set<std::string> &existing) {
    std::vector<std::string> names;
    for (const auto &[person, roles] : parse_performers(performers_str)) {
        bool has_role = std::any_of(roles.begin(), roles.end(), [](const std::string &role) {
            return role == "Performer" || contains(role, "MainArtist") ||
                   contains(role, "FeaturedArtist");
        });
        if (has_role && existing.count(person) == 0 && !vec_contains(names, person)) {
            names.push_back(person);
        }
    }
    return names;
}

std::vector<std::string> extract_composers_from_performers(const std::string &performers_str) {
    std::vector<std::string> composers;
    for (const auto &[person, roles] : parse_performers(performers_str)) {
        bool is_composer = std::any_of(roles.begin(), roles.end(), [](const std::string &role) {
            return contains(role, "Composer") || contains(role, "Lyricist");
        });
        if (is_composer && !vec_contains(composers, person)) {
            composers.push_back(person);
        }
    }
    return composers;
}

std::vector<std::string> extract_producers_from_performers(const std::string &performers_str) {
    std::vector<std::string> producers;
    for (const auto &[person, roles] : parse_performers(performers_str)) {
        if (vec_contains(roles, "Producer")) producers.push_back(person);
    }
    return producers;
}

std::string normalize_composer_name(const std::string &name) {
    std::string out;
    for (char c : name) {
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    out = trim(out);

    std::string no_punct;
    for (char c : out) {
        if (c == '.' || c == ',') continue;
        no_punct += (c == '-') ? ' ' : c;
    }

    size_t pos = 0;
    while ((pos = no_punct.find("  ")) != std::string::npos) {
        no_punct.replace(pos, 2, " ");
    }
    return trim(no_punct);
}

bool is_duplicate_composer(const std::string &name, const std::set<std::string> &existing) {
    std::string normalized = normalize_composer_name(name);
    if (existing.count(normalized)) return true;
    for (const auto &e : existing) {
        if (e.find(normalized) != std::string::npos ||
            normalized.find(e) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace kb
