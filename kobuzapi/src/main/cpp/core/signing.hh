#pragma once

// Port of src/signing.rs — MD5-based request signature generation.

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "ae/hash.hh"

namespace kb {

// Sorts params alphabetically by key, then hashes
// METHOD + endpoint + key1value1...keyNvalueN + app_secret.
inline std::string sign_request(const std::string &method, const std::string &endpoint,
                                std::vector<std::pair<std::string, std::string>> &params,
                                const std::string &app_secret) {
    std::sort(params.begin(), params.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });

    std::string input = method + endpoint;
    for (const auto &[key, value] : params) {
        input += key;
        input += value;
    }
    input += app_secret;
    return ae::md5_hex(input);
}

// Fixed-format hash for track file URL requests.
inline std::string sign_track_file_url(int format_id, int track_id,
                                       const std::string &timestamp,
                                       const std::string &app_secret) {
    std::string input = "trackgetFileUrlformat_id" + std::to_string(format_id) +
                        "intentstreamtrack_id" + std::to_string(track_id) + timestamp +
                        app_secret;
    return ae::md5_hex(input);
}

} // namespace kb
