#pragma once

// JSON (de)serialization for the models — port of serde derives plus the
// flexible helpers in src/models/deserialization.rs. Built on nlohmann::json
// ADL: `j.get<kb::Track>()` etc. Parsing failures throw; use kb::parse_json
// for the Result-returning boundary.

#include <memory>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "errors.hh"
#include "models.hh"

namespace nlohmann {

// Box<T> fields: null -> nullptr, value -> heap-allocated T.
template <typename T>
struct adl_serializer<std::shared_ptr<T>> {
    static void from_json(const json &j, std::shared_ptr<T> &p) {
        p = j.is_null() ? nullptr : std::make_shared<T>(j.template get<T>());
    }
    static void to_json(json &j, const std::shared_ptr<T> &p) {
        if (p) j = *p;
        else j = nullptr;
    }
};

template <typename T>
struct adl_serializer<std::optional<T>> {
    static void from_json(const json &j, std::optional<T> &o) {
        if (j.is_null()) o = std::nullopt;
        else o = j.template get<T>();
    }
    static void to_json(json &j, const std::optional<T> &o) {
        if (o) j = *o;
        else j = nullptr;
    }
};

} // namespace nlohmann

namespace kb {

namespace jsonu {

inline bool present(const nlohmann::json &j, const char *key) {
    auto it = j.find(key);
    return it != j.end() && !it->is_null();
}

// Missing/null-tolerant field read (serde's Option<T> semantics).
template <typename T>
void get_opt(const nlohmann::json &j, const char *key, T &out) {
    out = T{};
    if (present(j, key)) j.at(key).get_to(out);
}

// deserialize_flexible_name: string, {"display": "..."} or any scalar.
std::optional<std::string> flexible_name(const nlohmann::json &j, const char *key);

// deserialize_flexible_string_id: string or number, stringified.
std::optional<std::string> flexible_string_id(const nlohmann::json &j, const char *key);

// deserialize_picture: object -> Image, string URL / null -> absent.
std::optional<Image> picture(const nlohmann::json &j, const char *key);

} // namespace jsonu

void from_json(const nlohmann::json &j, Image &v);
void from_json(const nlohmann::json &j, Label &v);
void from_json(const nlohmann::json &j, Genre &v);
void from_json(const nlohmann::json &j, Biography &v);
void from_json(const nlohmann::json &j, Artist &v);
void from_json(const nlohmann::json &j, AudioInfo &v);
void from_json(const nlohmann::json &j, Album &v);
void from_json(const nlohmann::json &j, Track &v);
void from_json(const nlohmann::json &j, Credential &v);
void from_json(const nlohmann::json &j, Subscription &v);
void from_json(const nlohmann::json &j, User &v);
void from_json(const nlohmann::json &j, PlaylistOwner &v);
void from_json(const nlohmann::json &j, Playlist &v);
void from_json(const nlohmann::json &j, FileUrl &v);
void from_json(const nlohmann::json &j, AlbumSearchResponse &v);
void from_json(const nlohmann::json &j, ArtistSearchResponse &v);
void from_json(const nlohmann::json &j, TrackSearchResponse &v);
void from_json(const nlohmann::json &j, PlaylistSearchResponse &v);
void from_json(const nlohmann::json &j, SearchResult &v);
void from_json(const nlohmann::json &j, UserFavorites &v);

template <typename T>
void from_json(const nlohmann::json &j, ItemSearchResult<T> &v) {
    jsonu::get_opt(j, "items", v.items);
    jsonu::get_opt(j, "total", v.total);
    jsonu::get_opt(j, "limit", v.limit);
    jsonu::get_opt(j, "offset", v.offset);
}

// Parses a response body into T, converting throw-based failures into
// ApiResponseParse errors (content truncated like the Rust counterpart).
template <typename T>
Result<T> parse_json(const std::string &body) {
    try {
        return nlohmann::json::parse(body).get<T>();
    } catch (const std::exception &e) {
        std::string content = body.size() > 500 ? body.substr(0, 500) + "..." : body;
        return parse_error(content, e.what());
    }
}

} // namespace kb
